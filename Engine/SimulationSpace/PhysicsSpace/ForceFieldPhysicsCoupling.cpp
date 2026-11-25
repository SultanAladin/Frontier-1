/*============================================================================================================================================
                                                      FORCEFIELDPHYSICSCOUPLING.CPP
============================================================================================================================================*/
// 🧩 Wait-free ForceFieldRegister -> JoltPhysics rigid body & vehicle aerodynamic coupling + Stage F (Slice S24) two-way GPU collision readback
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §1.4 & §2.2A (Slices S06 & S24), BroadphaseSolver.{h,cpp}

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

namespace
{

[[nodiscard]] Mathematics::Axis3D ClampForceMagnitude(const Mathematics::Axis3D& f, const float maxNewtons) noexcept
{
    if (!std::isfinite(f.X) || !std::isfinite(f.Y) || !std::isfinite(f.Z))
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float magSq = f.X * f.X + f.Y * f.Y + f.Z * f.Z;
    if (magSq <= maxNewtons * maxNewtons || maxNewtons <= 0.0f)
    {
        return f;
    }
    const float scale = maxNewtons / std::sqrt(magSq);
    return { f.X * scale, f.Y * scale, f.Z * scale };
}

[[nodiscard]] std::array<float, 3> Cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

[[nodiscard]] float FetchHullVoxelCpu(
    const CompoundHullSdfHeaderGpu& hdr,
    const std::span<const float> voxels,
    const int32_t x,
    const int32_t y,
    const int32_t z) noexcept
{
    const int32_t cx = std::clamp(x, 0, static_cast<int32_t>(hdr.GridDimOffset[0]) - 1);
    const int32_t cy = std::clamp(y, 0, static_cast<int32_t>(hdr.GridDimOffset[1]) - 1);
    const int32_t cz = std::clamp(z, 0, static_cast<int32_t>(hdr.GridDimOffset[2]) - 1);
    const size_t idx = static_cast<size_t>(hdr.GridDimOffset[3])
                     + static_cast<size_t>(cz) * static_cast<size_t>(hdr.GridDimOffset[0]) * static_cast<size_t>(hdr.GridDimOffset[1])
                     + static_cast<size_t>(cy) * static_cast<size_t>(hdr.GridDimOffset[0])
                     + static_cast<size_t>(cx);
    return (idx < voxels.size()) ? voxels[idx] : 1.0e6f;
}

[[nodiscard]] float SampleHullSdfTrilinearCpu(
    const CompoundHullSdfHeaderGpu& hdr,
    const std::span<const float> voxels,
    const std::array<float, 3>& localPos) noexcept
{
    const float h = std::max(hdr.BoundsMinCellSize[3], 1.0e-5f);
    const float maxX = std::max(0.0f, static_cast<float>(hdr.GridDimOffset[0]) - 1.0001f);
    const float maxY = std::max(0.0f, static_cast<float>(hdr.GridDimOffset[1]) - 1.0001f);
    const float maxZ = std::max(0.0f, static_cast<float>(hdr.GridDimOffset[2]) - 1.0001f);
    const float cx = std::clamp((localPos[0] - hdr.BoundsMinCellSize[0]) / h - 0.5f, 0.0f, maxX);
    const float cy = std::clamp((localPos[1] - hdr.BoundsMinCellSize[1]) / h - 0.5f, 0.0f, maxY);
    const float cz = std::clamp((localPos[2] - hdr.BoundsMinCellSize[2]) / h - 0.5f, 0.0f, maxZ);

    const int32_t ix = static_cast<int32_t>(std::floor(cx));
    const int32_t iy = static_cast<int32_t>(std::floor(cy));
    const int32_t iz = static_cast<int32_t>(std::floor(cz));
    const float fx = cx - static_cast<float>(ix);
    const float fy = cy - static_cast<float>(iy);
    const float fz = cz - static_cast<float>(iz);

    const float c000 = FetchHullVoxelCpu(hdr, voxels, ix + 0, iy + 0, iz + 0);
    const float c100 = FetchHullVoxelCpu(hdr, voxels, ix + 1, iy + 0, iz + 0);
    const float c010 = FetchHullVoxelCpu(hdr, voxels, ix + 0, iy + 1, iz + 0);
    const float c110 = FetchHullVoxelCpu(hdr, voxels, ix + 1, iy + 1, iz + 0);
    const float c001 = FetchHullVoxelCpu(hdr, voxels, ix + 0, iy + 0, iz + 1);
    const float c101 = FetchHullVoxelCpu(hdr, voxels, ix + 1, iy + 0, iz + 1);
    const float c011 = FetchHullVoxelCpu(hdr, voxels, ix + 0, iy + 1, iz + 1);
    const float c111 = FetchHullVoxelCpu(hdr, voxels, ix + 1, iy + 1, iz + 1);

    const float c00 = c000 + (c100 - c000) * fx;
    const float c10 = c010 + (c110 - c010) * fx;
    const float c01 = c001 + (c101 - c001) * fx;
    const float c11 = c011 + (c111 - c011) * fx;
    const float c0  = c00 + (c10 - c00) * fy;
    const float c1  = c01 + (c11 - c01) * fy;
    return c0 + (c1 - c0) * fz;
}

[[nodiscard]] bool EvaluateCompoundHullLocalCpu(
    const CompoundHullSdfBank& bank,
    const uint32_t bodySlot,
    const std::array<float, 3>& relLocal,
    const float particleRadius,
    std::array<float, 3>& localNormal,
    std::array<float, 3>& localClosest,
    float& penetration) noexcept
{
    for (const auto& hdr : bank.Headers)
    {
        if (hdr.BodySlotPad[0] != bodySlot) continue;
        const float h = std::max(hdr.BoundsMinCellSize[3], 1.0e-5f);
        const float maxX = hdr.BoundsMinCellSize[0] + static_cast<float>(hdr.GridDimOffset[0]) * h;
        const float maxY = hdr.BoundsMinCellSize[1] + static_cast<float>(hdr.GridDimOffset[1]) * h;
        const float maxZ = hdr.BoundsMinCellSize[2] + static_cast<float>(hdr.GridDimOffset[2]) * h;
        if (relLocal[0] < hdr.BoundsMinCellSize[0] - particleRadius || relLocal[0] > maxX + particleRadius
         || relLocal[1] < hdr.BoundsMinCellSize[1] - particleRadius || relLocal[1] > maxY + particleRadius
         || relLocal[2] < hdr.BoundsMinCellSize[2] - particleRadius || relLocal[2] > maxZ + particleRadius)
        {
            return false;
        }

        const float sdf = SampleHullSdfTrilinearCpu(hdr, bank.Voxels, relLocal);
        if (sdf >= particleRadius) return false;

        const float eps = h * 0.5f;
        const float gx = SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0] + eps, relLocal[1], relLocal[2] })
                       - SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0] - eps, relLocal[1], relLocal[2] });
        const float gy = SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0], relLocal[1] + eps, relLocal[2] })
                       - SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0], relLocal[1] - eps, relLocal[2] });
        const float gz = SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0], relLocal[1], relLocal[2] + eps })
                       - SampleHullSdfTrilinearCpu(hdr, bank.Voxels, { relLocal[0], relLocal[1], relLocal[2] - eps });
        const float gLen = std::sqrt(gx * gx + gy * gy + gz * gz);
        if (gLen > 1.0e-6f)
        {
            localNormal = { gx / gLen, gy / gLen, gz / gLen };
        }
        else
        {
            localNormal = { 0.0f, 0.0f, 1.0f };
        }
        penetration  = particleRadius - sdf;
        localClosest = {
            relLocal[0] - localNormal[0] * sdf,
            relLocal[1] - localNormal[1] * sdf,
            relLocal[2] - localNormal[2] * sdf
        };
        return true;
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             COMPOUND HULL SDF BAKER
//------------------------------------------------------------------------------------------------------------------------

void CompoundHullSdfBank::AppendSphereUnionHull(
    const uint32_t bodySlotIndex,
    const std::span<const CompoundHullSubSphere> subSpheres,
    const float cellSizeMeters,
    const float paddingMeters)
{
    if (subSpheres.empty()) return;
    const float h = std::max(cellSizeMeters, 1.0e-3f);
    std::array<float, 3> bMin {  1.0e9f,  1.0e9f,  1.0e9f };
    std::array<float, 3> bMax { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& s : subSpheres)
    {
        for (size_t a = 0u; a < 3u; ++a)
        {
            bMin[a] = std::min(bMin[a], s.LocalCenterMeters[a] - s.RadiusMeters - paddingMeters);
            bMax[a] = std::max(bMax[a], s.LocalCenterMeters[a] + s.RadiusMeters + paddingMeters);
        }
    }

    const uint32_t nx = std::max(4u, static_cast<uint32_t>(std::ceil((bMax[0] - bMin[0]) / h)));
    const uint32_t ny = std::max(4u, static_cast<uint32_t>(std::ceil((bMax[1] - bMin[1]) / h)));
    const uint32_t nz = std::max(4u, static_cast<uint32_t>(std::ceil((bMax[2] - bMin[2]) / h)));

    CompoundHullSdfHeaderGpu hdr {};
    hdr.BoundsMinCellSize = { bMin[0], bMin[1], bMin[2], h };
    hdr.GridDimOffset     = { nx, ny, nz, static_cast<uint32_t>(Voxels.size()) };
    hdr.BodySlotPad       = { bodySlotIndex, 0u, 0u, 0u };
    Headers.push_back(hdr);

    Voxels.reserve(Voxels.size() + static_cast<size_t>(nx) * static_cast<size_t>(ny) * static_cast<size_t>(nz));
    for (uint32_t z = 0u; z < nz; ++z)
    {
        const float wz = bMin[2] + (static_cast<float>(z) + 0.5f) * h;
        for (uint32_t y = 0u; y < ny; ++y)
        {
            const float wy = bMin[1] + (static_cast<float>(y) + 0.5f) * h;
            for (uint32_t x = 0u; x < nx; ++x)
            {
                const float wx = bMin[0] + (static_cast<float>(x) + 0.5f) * h;
                float bestSdf = 1.0e9f;
                for (const auto& s : subSpheres)
                {
                    const float dx = wx - s.LocalCenterMeters[0];
                    const float dy = wy - s.LocalCenterMeters[1];
                    const float dz = wz - s.LocalCenterMeters[2];
                    const float d  = std::sqrt(dx * dx + dy * dy + dz * dz) - s.RadiusMeters;
                    bestSdf = std::min(bestSdf, d);
                }
                Voxels.push_back(bestSdf);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PURE FORCE COUPLING COMPUTATION
//------------------------------------------------------------------------------------------------------------------------

Mathematics::Axis3D ComputeCoupledRigidForceNewtons(
    const CoupledRigidBodyEntry& entry,
    const ForceFields::PublishedForceFieldReading& reading,
    const RigidBodyReading& bodyReading,
    const double simulationSeconds) noexcept
{
    if (!bodyReading.Resident || bodyReading.MassKg <= 0.0f || reading.ActiveCount == 0u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const std::array<float, 3> worldPos { bodyReading.PositionMeters.X, bodyReading.PositionMeters.Y, bodyReading.PositionMeters.Z };
    const std::array<float, 3> worldVel { bodyReading.LinearVelocity.X, bodyReading.LinearVelocity.Y, bodyReading.LinearVelocity.Z };
    const std::array<float, 3> fieldVec = ForceFields::EvaluateTotalForceField(
        reading.ActiveSpan(),
        worldPos,
        worldVel,
        static_cast<float>(simulationSeconds),
        entry.ChannelMaskBits);

    Mathematics::Axis3D forceN { 0.0f, 0.0f, 0.0f };
    if (entry.CouplingMode == ForceCouplingMode::MassProportionalAcceleration)
    {
        const float factor = bodyReading.MassKg * entry.CouplingScale;
        forceN = { fieldVec[0] * factor, fieldVec[1] * factor, fieldVec[2] * factor };
    }
    else
    {
        const float relX = fieldVec[0] - worldVel[0];
        const float relY = fieldVec[1] - worldVel[1];
        const float relZ = fieldVec[2] - worldVel[2];
        const float relSpeed = std::sqrt(relX * relX + relY * relY + relZ * relZ);
        const float qFactor = 0.5f * entry.AirDensityKgPerM3 * entry.DragCoefficient * entry.DragAreaM2 * relSpeed * entry.CouplingScale;
        forceN = { relX * qFactor, relY * qFactor, relZ * qFactor };
    }

    return ClampForceMagnitude(forceN, entry.MaximumForceNewtons);
}

Mathematics::Axis3D ComputeVehicleWindForceNewtons(
    const ForceFields::PublishedForceFieldReading& reading,
    const RigidBodyReading& chassisReading,
    const float frontalAreaM2,
    const float sideAreaM2,
    const float dragCoefficient,
    const float airDensityKgPerM3,
    const double simulationSeconds) noexcept
{
    if (!chassisReading.Resident || chassisReading.MassKg <= 0.0f || reading.ActiveCount == 0u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const std::array<float, 3> worldPos { chassisReading.PositionMeters.X, chassisReading.PositionMeters.Y, chassisReading.PositionMeters.Z };
    const std::array<float, 3> zeroVel  { 0.0f, 0.0f, 0.0f };
    const std::array<float, 3> windAir = ForceFields::EvaluateTotalForceField(
        reading.ActiveSpan(),
        worldPos,
        zeroVel,
        static_cast<float>(simulationSeconds),
        static_cast<uint32_t>(ForceFields::ForceTargetChannelBits::Rigid));

    const Mathematics::Axis3D relWorld {
        windAir[0] - chassisReading.LinearVelocity.X,
        windAir[1] - chassisReading.LinearVelocity.Y,
        windAir[2] - chassisReading.LinearVelocity.Z
    };

    const Mathematics::Axis3D relLocal = RigidBodyDynamics::RotateByConjugateQuaternion(chassisReading.Orientation, relWorld);
    const float qHalf = 0.5f * airDensityKgPerM3 * dragCoefficient;

    const Mathematics::Axis3D localForce {
        qHalf * frontalAreaM2 * std::fabs(relLocal.X) * relLocal.X,
        qHalf * sideAreaM2    * std::fabs(relLocal.Y) * relLocal.Y,
        qHalf * frontalAreaM2 * 0.5f * std::fabs(relLocal.Z) * relLocal.Z
    };

    return ClampForceMagnitude(RigidBodyDynamics::RotateByQuaternion(chassisReading.Orientation, localForce), 250000.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                 SLICE S24 CPU COLLISION MIRROR & READBACK DECODER
//------------------------------------------------------------------------------------------------------------------------

void ResolveParticleRigidCollisionsCpu(
    const ParticleCollisionUniformBlock& uniforms,
    const std::span<Frontier::ParticleSpace::ParticleStateGpuRecord> particles,
    const std::span<const RigidColliderProxy> proxies,
    const std::span<const std::array<float, 4>> proxyAngularVelocities,
    const CompoundHullSdfBank& hullBank,
    std::vector<RigidImpulseAtomicGpu>& outAtomics)
{
    outAtomics.assign(proxies.size(), RigidImpulseAtomicGpu{});
    for (size_t p = 0u; p < proxies.size(); ++p)
    {
        outAtomics[p].BodySlotIndex = proxies[p].BodySlotIndex;
    }

    const size_t particleCount = std::min(particles.size(), static_cast<size_t>(std::max(0.0f, uniforms.GroundHeightFlags[3])));
    const float  lifetime      = uniforms.ScaleLifetime[1];
    const float  pRadius       = std::max(uniforms.ParticleMaterial[0], 1.0e-4f);
    const float  pMass         = std::max(uniforms.ParticleMaterial[1], 1.0e-5f);
    const float  defRest       = std::clamp(uniforms.ParticleMaterial[2], 0.0f, 1.0f);
    const float  defFric       = std::clamp(uniforms.ParticleMaterial[3], 0.0f, 2.0f);
    const uint32_t analyticCnt = std::min(static_cast<uint32_t>(std::max(0.0f, uniforms.GroundHeightFlags[2])), 4u);
    const uint32_t channelMask = uniforms.CountsMasks[2];
    const bool   twoWay        = (uniforms.CountsMasks[3] != 0u);
    const float  fixedScale    = std::max(uniforms.ScaleLifetime[0], 1.0f);
    const float  maxImpulse    = std::max(uniforms.ScaleLifetime[2], 1.0f);

    for (size_t i = 0u; i < particleCount; ++i)
    {
        auto& st = particles[i];
        if (lifetime > 0.0f && st.PositionAge[3] >= lifetime) continue;

        std::array<float, 3> pos { st.PositionAge[0],  st.PositionAge[1],  st.PositionAge[2] };
        std::array<float, 3> vel { st.VelocitySeed[0], st.VelocitySeed[1], st.VelocitySeed[2] };

        // 1. Analytic ground
        if (uniforms.GroundHeightFlags[1] > 0.5f)
        {
            const float groundY = uniforms.GroundHeightFlags[0] + pRadius;
            if (pos[1] < groundY)
            {
                pos[1] = groundY;
                if (vel[1] < 0.0f)
                {
                    const float normalDelta = -(1.0f + defRest) * vel[1];
                    vel[1] += normalDelta;
                    const float tanLen = std::sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
                    if (tanLen > 1.0e-6f && defFric > 0.0f)
                    {
                        const float maxF = std::min(tanLen, defFric * normalDelta);
                        vel[0] -= (vel[0] / tanLen) * maxF;
                        vel[2] -= (vel[2] / tanLen) * maxF;
                    }
                }
            }
        }

        // 2. Analytic 4 spheres (ColliderChunk parity)
        for (uint32_t s = 0u; s < analyticCnt; ++s)
        {
            const auto& sph = uniforms.AnalyticColliders[s];
            const float minD = std::max(sph[3], 1.0e-4f) + pRadius;
            const float dx = pos[0] - sph[0], dy = pos[1] - sph[1], dz = pos[2] - sph[2];
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < minD * minD)
            {
                const float dist = std::sqrt(std::max(distSq, 1.0e-12f));
                const float nx = (dist > 1.0e-5f) ? (dx / dist) : 0.0f;
                const float ny = (dist > 1.0e-5f) ? (dy / dist) : 1.0f;
                const float nz = (dist > 1.0e-5f) ? (dz / dist) : 0.0f;
                pos[0] = sph[0] + nx * minD;
                pos[1] = sph[1] + ny * minD;
                pos[2] = sph[2] + nz * minD;
                const float vn = vel[0] * nx + vel[1] * ny + vel[2] * nz;
                if (vn < 0.0f)
                {
                    const float normalDelta = -(1.0f + defRest) * vn;
                    vel[0] += nx * normalDelta;
                    vel[1] += ny * normalDelta;
                    vel[2] += nz * normalDelta;
                    const float newVn = vel[0] * nx + vel[1] * ny + vel[2] * nz;
                    const float tx = vel[0] - nx * newVn;
                    const float ty = vel[1] - ny * newVn;
                    const float tz = vel[2] - nz * newVn;
                    const float tanLen = std::sqrt(tx * tx + ty * ty + tz * tz);
                    if (tanLen > 1.0e-6f && defFric > 0.0f)
                    {
                        const float maxF = std::min(tanLen, defFric * normalDelta);
                        vel[0] -= (tx / tanLen) * maxF;
                        vel[1] -= (ty / tanLen) * maxF;
                        vel[2] -= (tz / tanLen) * maxF;
                    }
                }
            }
        }

        // 3. 80-byte RigidColliderProxy SSBO (Sphere, OrientedBox, Capsule +Z, CompoundHull SDF)
        for (size_t p = 0u; p < proxies.size(); ++p)
        {
            const auto& proxy = proxies[p];
            if ((proxy.ChannelMask & channelMask) == 0u) continue;

            const RigidOrientationQuaternion quat {
                proxy.OrientationQuat[0], proxy.OrientationQuat[1], proxy.OrientationQuat[2], proxy.OrientationQuat[3]
            };
            const Mathematics::Axis3D relWorld {
                pos[0] - proxy.CenterMeters[0],
                pos[1] - proxy.CenterMeters[1],
                pos[2] - proxy.CenterMeters[2]
            };
            const Mathematics::Axis3D rl = RigidBodyDynamics::RotateByConjugateQuaternion(quat, relWorld);
            const std::array<float, 3> relLocal { rl.X, rl.Y, rl.Z };

            std::array<float, 3> localNormal  { 0.0f, 0.0f, 1.0f };
            std::array<float, 3> localClosest { 0.0f, 0.0f, 0.0f };
            float penetration = 0.0f;
            bool hit = false;

            if (proxy.ShapeCode == 0u)
            {
                const float minD = std::max(proxy.RadiusMeters, 1.0e-4f) + pRadius;
                const float distSq = relLocal[0] * relLocal[0] + relLocal[1] * relLocal[1] + relLocal[2] * relLocal[2];
                if (distSq < minD * minD)
                {
                    const float dist = std::sqrt(std::max(distSq, 1.0e-12f));
                    localNormal = (dist > 1.0e-5f)
                        ? std::array<float, 3>{ relLocal[0] / dist, relLocal[1] / dist, relLocal[2] / dist }
                        : std::array<float, 3>{ 0.0f, 0.0f, 1.0f };
                    localClosest = {
                        localNormal[0] * proxy.RadiusMeters,
                        localNormal[1] * proxy.RadiusMeters,
                        localNormal[2] * proxy.RadiusMeters
                    };
                    penetration = minD - dist;
                    hit = true;
                }
            }
            else if (proxy.ShapeCode == 1u)
            {
                const float hx = std::max(proxy.HalfExtentsMeters[0], 1.0e-4f);
                const float hy = std::max(proxy.HalfExtentsMeters[1], 1.0e-4f);
                const float hz = std::max(proxy.HalfExtentsMeters[2], 1.0e-4f);
                localClosest = {
                    std::clamp(relLocal[0], -hx, hx),
                    std::clamp(relLocal[1], -hy, hy),
                    std::clamp(relLocal[2], -hz, hz)
                };
                const float dx = relLocal[0] - localClosest[0];
                const float dy = relLocal[1] - localClosest[1];
                const float dz = relLocal[2] - localClosest[2];
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq > 1.0e-10f)
                {
                    if (distSq < pRadius * pRadius)
                    {
                        const float dist = std::sqrt(distSq);
                        localNormal = { dx / dist, dy / dist, dz / dist };
                        penetration = pRadius - dist;
                        hit = true;
                    }
                }
                else
                {
                    const float ex = hx - std::fabs(relLocal[0]);
                    const float ey = hy - std::fabs(relLocal[1]);
                    const float ez = hz - std::fabs(relLocal[2]);
                    if (ex <= ey && ex <= ez)
                    {
                        localNormal = { relLocal[0] >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
                        localClosest[0] = localNormal[0] * hx;
                        penetration = ex + pRadius;
                    }
                    else if (ey <= ez)
                    {
                        localNormal = { 0.0f, relLocal[1] >= 0.0f ? 1.0f : -1.0f, 0.0f };
                        localClosest[1] = localNormal[1] * hy;
                        penetration = ey + pRadius;
                    }
                    else
                    {
                        localNormal = { 0.0f, 0.0f, relLocal[2] >= 0.0f ? 1.0f : -1.0f };
                        localClosest[2] = localNormal[2] * hz;
                        penetration = ez + pRadius;
                    }
                    hit = true;
                }
            }
            else if (proxy.ShapeCode == 2u)
            {
                const float halfH = std::max(proxy.HalfExtentsMeters[2], 0.0f);
                const float minD  = std::max(proxy.RadiusMeters, 1.0e-4f) + pRadius;
                const float cz    = std::clamp(relLocal[2], -halfH, halfH);
                const float dx = relLocal[0], dy = relLocal[1], dz = relLocal[2] - cz;
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq < minD * minD)
                {
                    const float dist = std::sqrt(std::max(distSq, 1.0e-12f));
                    localNormal = (dist > 1.0e-5f)
                        ? std::array<float, 3>{ dx / dist, dy / dist, dz / dist }
                        : std::array<float, 3>{ 0.0f, 0.0f, 1.0f };
                    localClosest = {
                        localNormal[0] * proxy.RadiusMeters,
                        localNormal[1] * proxy.RadiusMeters,
                        cz + localNormal[2] * proxy.RadiusMeters
                    };
                    penetration = minD - dist;
                    hit = true;
                }
            }
            else
            {
                hit = EvaluateCompoundHullLocalCpu(hullBank, proxy.BodySlotIndex, relLocal, pRadius, localNormal, localClosest, penetration);
            }

            if (!hit) continue;

            const auto wn = RigidBodyDynamics::RotateByQuaternion(quat, { localNormal[0], localNormal[1], localNormal[2] });
            const auto la = RigidBodyDynamics::RotateByQuaternion(quat, { localClosest[0], localClosest[1], localClosest[2] });
            const std::array<float, 3> leverArm { la.X, la.Y, la.Z };
            const std::array<float, 3> contactPoint {
                proxy.CenterMeters[0] + la.X,
                proxy.CenterMeters[1] + la.Y,
                proxy.CenterMeters[2] + la.Z
            };
            pos[0] += wn.X * penetration;
            pos[1] += wn.Y * penetration;
            pos[2] += wn.Z * penetration;

            const std::array<float, 3> omega = (p < proxyAngularVelocities.size())
                ? std::array<float, 3>{ proxyAngularVelocities[p][0], proxyAngularVelocities[p][1], proxyAngularVelocities[p][2] }
                : std::array<float, 3>{ 0.0f, 0.0f, 0.0f };
            const auto omegaCrossR = Cross3(omega, leverArm);
            const std::array<float, 3> surfVel {
                proxy.VelocityMps[0] + omegaCrossR[0],
                proxy.VelocityMps[1] + omegaCrossR[1],
                proxy.VelocityMps[2] + omegaCrossR[2]
            };
            const std::array<float, 3> relVel { vel[0] - surfVel[0], vel[1] - surfVel[1], vel[2] - surfVel[2] };
            const float relVn = relVel[0] * wn.X + relVel[1] * wn.Y + relVel[2] * wn.Z;

            if (relVn < 0.0f)
            {
                const float rest = std::clamp(proxy.Restitution, 0.0f, 1.0f);
                const float fric = std::clamp(proxy.Friction, 0.0f, 2.0f);
                const float normalDelta = -(1.0f + rest) * relVn;
                std::array<float, 3> deltaV { wn.X * normalDelta, wn.Y * normalDelta, wn.Z * normalDelta };

                const std::array<float, 3> tanV {
                    relVel[0] - wn.X * relVn,
                    relVel[1] - wn.Y * relVn,
                    relVel[2] - wn.Z * relVn
                };
                const float tanLenSq = tanV[0] * tanV[0] + tanV[1] * tanV[1] + tanV[2] * tanV[2];
                if (tanLenSq > 1.0e-10f && fric > 0.0f)
                {
                    const float tanLen = std::sqrt(tanLenSq);
                    const float maxF   = std::min(tanLen, fric * normalDelta);
                    const float scale  = maxF / tanLen;
                    deltaV[0] -= tanV[0] * scale;
                    deltaV[1] -= tanV[1] * scale;
                    deltaV[2] -= tanV[2] * scale;
                }

                vel[0] += deltaV[0];
                vel[1] += deltaV[1];
                vel[2] += deltaV[2];

                if (twoWay && proxy.DynamicResponseBit != 0u)
                {
                    std::array<float, 3> bodyLinJ { -deltaV[0] * pMass, -deltaV[1] * pMass, -deltaV[2] * pMass };
                    const float jMag = std::sqrt(bodyLinJ[0] * bodyLinJ[0] + bodyLinJ[1] * bodyLinJ[1] + bodyLinJ[2] * bodyLinJ[2]);
                    if (jMag > maxImpulse)
                    {
                        const float s = maxImpulse / jMag;
                        bodyLinJ[0] *= s; bodyLinJ[1] *= s; bodyLinJ[2] *= s;
                    }
                    const auto bodyAngJ = Cross3(leverArm, bodyLinJ);

                    auto& acc = outAtomics[p];
                    acc.LinearImpulseFixed[0]   += static_cast<int32_t>(std::lround(bodyLinJ[0] * fixedScale));
                    acc.LinearImpulseFixed[1]   += static_cast<int32_t>(std::lround(bodyLinJ[1] * fixedScale));
                    acc.LinearImpulseFixed[2]   += static_cast<int32_t>(std::lround(bodyLinJ[2] * fixedScale));
                    acc.AngularImpulseFixed[0]  += static_cast<int32_t>(std::lround(bodyAngJ[0] * fixedScale));
                    acc.AngularImpulseFixed[1]  += static_cast<int32_t>(std::lround(bodyAngJ[1] * fixedScale));
                    acc.AngularImpulseFixed[2]  += static_cast<int32_t>(std::lround(bodyAngJ[2] * fixedScale));
                    acc.ContactPointSumFixed[0] += static_cast<int32_t>(std::lround(contactPoint[0] * fixedScale));
                    acc.ContactPointSumFixed[1] += static_cast<int32_t>(std::lround(contactPoint[1] * fixedScale));
                    acc.ContactPointSumFixed[2] += static_cast<int32_t>(std::lround(contactPoint[2] * fixedScale));
                    acc.ContactCount            += 1u;
                }
            }
        }

        st.PositionAge[0]  = pos[0]; st.PositionAge[1]  = pos[1]; st.PositionAge[2]  = pos[2];
        st.VelocitySeed[0] = vel[0]; st.VelocitySeed[1] = vel[1]; st.VelocitySeed[2] = vel[2];
    }
}

void DecodeGpuImpulseReadback(
    const std::span<const RigidImpulseAtomicGpu> atomics,
    const std::span<const RigidBodyToken> proxyBodyTokens,
    const float fixedPointScale,
    std::vector<RigidImpulseAccumulatorEntry>& outEntries)
{
    outEntries.clear();
    const float invScale = 1.0f / std::max(fixedPointScale, 1.0f);
    for (size_t i = 0u; i < atomics.size(); ++i)
    {
        const auto& a = atomics[i];
        if (a.ContactCount == 0u) continue;
        RigidImpulseAccumulatorEntry entry {};
        if (i < proxyBodyTokens.size())
        {
            entry.BodyToken = proxyBodyTokens[i];
        }
        else
        {
            entry.BodyToken.SlotIndex  = a.BodySlotIndex;
            entry.BodyToken.Generation = 1u;
        }
        entry.ContactCount = a.ContactCount;
        const float invCnt = 1.0f / static_cast<float>(a.ContactCount);
        for (size_t c = 0u; c < 3u; ++c)
        {
            entry.LinearImpulse[c]  = static_cast<float>(a.LinearImpulseFixed[c])   * invScale;
            entry.AngularImpulse[c] = static_cast<float>(a.AngularImpulseFixed[c])  * invScale;
            entry.ContactPoint[c]   = static_cast<float>(a.ContactPointSumFixed[c]) * invScale * invCnt;
        }
        outEntries.push_back(entry);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FORCEFIELDPHYSICSCOUPLING
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldPhysicsCoupling::RegisterRigidBody(const CoupledRigidBodyEntry& entry)
{
    for (CoupledRigidBodyEntry& existing : CoupledBodies)
    {
        if (existing.BodyToken == entry.BodyToken)
        {
            existing = entry;
            return;
        }
    }
    CoupledBodies.push_back(entry);
}

bool ForceFieldPhysicsCoupling::UnregisterRigidBody(const RigidBodyToken bodyToken) noexcept
{
    for (auto it = CoupledBodies.begin(); it != CoupledBodies.end(); ++it)
    {
        if (it->BodyToken == bodyToken)
        {
            CoupledBodies.erase(it);
            return true;
        }
    }
    return false;
}

ForceCouplingStepSummary ForceFieldPhysicsCoupling::ApplyPublishedForceFields(
    PhysicsExtension& physics,
    const ForceFields::ForceFieldRegister& forceRegister,
    const double simulationSeconds) const noexcept
{
    ForceCouplingStepSummary summary {};
    if (CoupledBodies.empty()) return summary;

    auto lease = forceRegister.AcquirePublishedReading();
    if (!lease.Valid()) return summary;
    const auto& reading = lease.InspectPayload();
    if (reading.ActiveCount == 0u) return summary;

    for (const CoupledRigidBodyEntry& entry : CoupledBodies)
    {
        if (!entry.BodyToken.Valid()) continue;
        const RigidBodyReading bodyReading = physics.InspectBodyReading(entry.BodyToken);
        if (!bodyReading.Resident || bodyReading.MassKg <= 0.0f) continue;

        const Mathematics::Axis3D forceN = ComputeCoupledRigidForceNewtons(entry, reading, bodyReading, simulationSeconds);
        const float mag = std::sqrt(forceN.X * forceN.X + forceN.Y * forceN.Y + forceN.Z * forceN.Z);
        if (mag > 1.0e-6f)
        {
            physics.ApplyForce(entry.BodyToken, forceN);
            ++summary.CoupledBodyCount;
            summary.PeakForceNewtons = std::max(summary.PeakForceNewtons, mag);
            summary.TotalForceNewtons += mag;
        }
    }
    return summary;
}

Mathematics::Axis3D ForceFieldPhysicsCoupling::ApplyPublishedWindToVehicleChassis(
    PhysicsExtension& physics,
    const RigidBodyToken chassisToken,
    const ForceFields::ForceFieldRegister& forceRegister,
    const float frontalAreaM2,
    const float sideAreaM2,
    const float dragCoefficient,
    const double simulationSeconds,
    const float airDensityKgPerM3) const noexcept
{
    if (!chassisToken.Valid()) return { 0.0f, 0.0f, 0.0f };
    auto lease = forceRegister.AcquirePublishedReading();
    if (!lease.Valid()) return { 0.0f, 0.0f, 0.0f };

    const RigidBodyReading chassisReading = physics.InspectBodyReading(chassisToken);
    const Mathematics::Axis3D forceN = ComputeVehicleWindForceNewtons(
        lease.InspectPayload(),
        chassisReading,
        frontalAreaM2,
        sideAreaM2,
        dragCoefficient,
        airDensityKgPerM3,
        simulationSeconds);

    const float magSq = forceN.X * forceN.X + forceN.Y * forceN.Y + forceN.Z * forceN.Z;
    if (magSq > 1.0e-12f)
    {
        physics.ApplyForce(chassisToken, forceN);
    }
    return forceN;
}

uint32_t ForceFieldPhysicsCoupling::ApplyGpuImpulseReadback(
    PhysicsExtension& physics,
    const std::span<const RigidImpulseAccumulatorEntry> readbackEntries) const noexcept
{
    uint32_t appliedCount = 0u;
    for (const RigidImpulseAccumulatorEntry& entry : readbackEntries)
    {
        if (!entry.BodyToken.Valid() || entry.ContactCount == 0u) continue;
        const Mathematics::Axis3D impulse { entry.LinearImpulse[0], entry.LinearImpulse[1], entry.LinearImpulse[2] };
        const Mathematics::Axis3D contact { entry.ContactPoint[0],  entry.ContactPoint[1],  entry.ContactPoint[2] };
        const float magSq = impulse.X * impulse.X + impulse.Y * impulse.Y + impulse.Z * impulse.Z;
        if (magSq > 1.0e-12f)
        {
            physics.ApplyImpulseAtPoint(entry.BodyToken, impulse, contact);
            ++appliedCount;
        }
    }
    return appliedCount;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
