/*============================================================================================================================================
                                                       XPBDCONSTRAINTSOLVER.CPP
============================================================================================================================================*/
// 🧩 Phase D02 Unified XPBD + 3D Stable Neo-Hookean FEM + Pneumatic Tire step integrator (exact CPU reference & parity solver)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"
#include "Engine/SimulationSpace/Deformable/NeoHookeanTetSolver.h"
#include "Engine/SimulationSpace/Deformable/PneumaticTireSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  XPBD CONSTRAINT SOLVER
//------------------------------------------------------------------------------------------------------------------------

DeformableCouplingFeedback XpbdConstraintSolver::Step(
    DeformableBody& body,
    float dtSeconds,
    const DeformableSimulationConfiguration& cfg,
    float simulationTimeSec,
    float windPulse,
    const DeformableExternalCoupling* externalCoupling)
{
    DeformableCouplingFeedback totalFeedback {};
    const uint32_t substeps = std::max(1u, cfg.Substeps);
    const float h           = std::min(dtSeconds, 1.0f / 30.0f) / static_cast<float>(substeps);
    const float invH        = 1.0f / h;
    const float invH2       = invH * invH;

    const uint32_t n        = body.PointCount;
    float* pos              = body.Positions.data();
    float* prev             = body.PrevPositions.data();
    float* vel              = body.Velocities.data();
    const float* invM       = body.InverseMass.data();
    const float* rad        = body.ContactRadius.data();
    const float* aero       = body.AeroDragScale.data();

    const float damp        = std::pow(cfg.Damping, 1.0f / static_cast<float>(substeps));
    const float stretchMult = cfg.StretchStiffness / 1200.0f;
    const float bendMult    = cfg.BendStiffness;
    const float tearGlobal  = cfg.TearRatio;

    const bool  colOn       = body.Collider.Active && cfg.ColliderEnabled;
    const float cx          = body.Collider.CenterMeters[0];
    const float cy          = body.Collider.CenterMeters[1];
    const float cz          = body.Collider.CenterMeters[2];
    const float cr          = body.Collider.RadiusMeters;

    const float maxSpeed    = std::max(14.0f, std::fabs(cfg.TireSpinRadPerSec) * 2.8f + 14.0f);
    const float maxSpeed2   = maxSpeed * maxSpeed;

    for (uint32_t s = 0u; s < substeps; ++s)
    {
        const float subT = simulationTimeSec + static_cast<float>(s) * h;

        // 0. Pneumatic XPBD & 3D Volumetric FEM tire co-rotating transport, Boyle bulge & centrifugal forces
        if (!body.PneumaticTires.empty() || !body.FemTires.empty())
        {
            PneumaticTireSolver::ApplySubstepTireForces(body, h, cfg);
        }

        // 1. External forces (gravity, curl wind + ForceFieldSampler, aquatic/air drag) & symplectic predict
        for (uint32_t i = 0u; i < n; ++i)
        {
            const size_t i3 = static_cast<size_t>(i) * 3u;
            prev[i3]      = pos[i3];
            prev[i3 + 1u] = pos[i3 + 1u];
            prev[i3 + 2u] = pos[i3 + 2u];

            if (invM[i] <= 0.0f)
            {
                continue;
            }

            const std::array<float, 3> wv = DeformablePhysicsCoupling::SampleWindForceVelocity(
                pos[i3], pos[i3 + 1u], pos[i3 + 2u], subT, cfg, windPulse, externalCoupling);
            const float drag = (body.UnderwaterEnabled ? 3.2f : 1.55f) * aero[i];
            const float ax   = (wv[0] - vel[i3]) * drag;
            const float ay   = cfg.GravityY + (wv[1] - vel[i3 + 1u]) * drag;
            const float az   = (wv[2] - vel[i3 + 2u]) * drag;

            vel[i3]      = (vel[i3] + ax * h) * damp;
            vel[i3 + 1u] = (vel[i3 + 1u] + ay * h) * damp;
            vel[i3 + 2u] = (vel[i3 + 2u] + az * h) * damp;

            const float sp2 = vel[i3] * vel[i3] + vel[i3 + 1u] * vel[i3 + 1u] + vel[i3 + 2u] * vel[i3 + 2u];
            if (sp2 > maxSpeed2)
            {
                const float sc = maxSpeed / std::sqrt(sp2);
                vel[i3] *= sc;
                vel[i3 + 1u] *= sc;
                vel[i3 + 2u] *= sc;
            }

            pos[i3]      += vel[i3] * h;
            pos[i3 + 1u] += vel[i3 + 1u] * h;
            pos[i3 + 2u] += vel[i3 + 2u] * h;
        }

        // Interactive grab pin
        if (body.GrabbedPointIndex >= 0 && static_cast<uint32_t>(body.GrabbedPointIndex) < n)
        {
            const size_t g3 = static_cast<size_t>(body.GrabbedPointIndex) * 3u;
            pos[g3]      = body.GrabTargetMeters[0];
            pos[g3 + 1u] = body.GrabTargetMeters[1];
            pos[g3 + 2u] = body.GrabTargetMeters[2];
        }

        // Reset XPBD dual multipliers per substep
        std::fill(body.DistanceDuals.begin(), body.DistanceDuals.end(), 0.0f);
        std::fill(body.BendDuals.begin(), body.BendDuals.end(), 0.0f);

        // 2. Distance / Structural / Shear / Tearable XPBD Constraints (respecting conflict color batches if built)
        const auto solveDistanceAt = [&](uint32_t k)
        {
            DistanceConstraintEntry& c = body.DistanceConstraints[k];
            if (c.Severed)
            {
                return;
            }
            const float wa = (static_cast<int32_t>(c.PointA) == body.GrabbedPointIndex) ? 0.0f : invM[c.PointA];
            const float wb = (static_cast<int32_t>(c.PointB) == body.GrabbedPointIndex) ? 0.0f : invM[c.PointB];
            const float wSum = wa + wb;
            if (wSum <= 0.0f)
            {
                return;
            }

            const size_t a3 = static_cast<size_t>(c.PointA) * 3u;
            const size_t b3 = static_cast<size_t>(c.PointB) * 3u;
            const float dx = pos[b3] - pos[a3];
            const float dy = pos[b3 + 1u] - pos[a3 + 1u];
            const float dz = pos[b3 + 2u] - pos[a3 + 2u];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 1.0e-6f)
            {
                return;
            }

            const float ratio = dist / c.RestLength;
            c.LiveStrain = std::fabs(ratio - 1.0f);
            const float br = (c.BreakRatio > 0.0f && tearGlobal > 0.0f) ? (c.BreakRatio * (tearGlobal / 2.4f)) : 0.0f;
            if (br > 1.05f && ratio > br)
            {
                c.Severed = true;
                ++body.SeveredCount;
                return;
            }

            const float C          = dist - c.RestLength;
            const float kEff       = std::max(10.0f, c.Stiffness * stretchMult);
            const float alphaTilde = invH2 / kEff;
            const float dl         = (-C - alphaTilde * body.DistanceDuals[k]) / (wSum + alphaTilde);
            body.DistanceDuals[k] += dl;
            const float corr       = dl / dist;

            if (wa > 0.0f)
            {
                pos[a3]      -= dx * corr * wa;
                pos[a3 + 1u] -= dy * corr * wa;
                pos[a3 + 2u] -= dz * corr * wa;
            }
            if (wb > 0.0f)
            {
                pos[b3]      += dx * corr * wb;
                pos[b3 + 1u] += dy * corr * wb;
                pos[b3 + 2u] += dz * corr * wb;
            }
        };

        if (body.DistanceBatches.ColorCount > 0u &&
            body.DistanceBatches.OrderedOrdinals.size() == body.DistanceConstraints.size())
        {
            for (uint32_t c = 0u; c < body.DistanceBatches.ColorCount; ++c)
            {
                const uint32_t begin = body.DistanceBatches.BatchOffsets[c];
                const uint32_t end   = body.DistanceBatches.BatchOffsets[c + 1u];
                for (uint32_t idx = begin; idx < end; ++idx)
                {
                    solveDistanceAt(body.DistanceBatches.OrderedOrdinals[idx]);
                }
            }
        }
        else
        {
            for (uint32_t k = 0u; k < static_cast<uint32_t>(body.DistanceConstraints.size()); ++k)
            {
                solveDistanceAt(k);
            }
        }

        // 3. Flexural Bending Constraints (distance-2)
        if (bendMult > 0.005f && !body.BendConstraints.empty())
        {
            for (uint32_t k = 0u; k < static_cast<uint32_t>(body.BendConstraints.size()); ++k)
            {
                const BendConstraintEntry& c = body.BendConstraints[k];
                const float wa = (static_cast<int32_t>(c.PointA) == body.GrabbedPointIndex) ? 0.0f : invM[c.PointA];
                const float wb = (static_cast<int32_t>(c.PointB) == body.GrabbedPointIndex) ? 0.0f : invM[c.PointB];
                const float wSum = wa + wb;
                if (wSum <= 0.0f)
                {
                    continue;
                }
                const size_t a3 = static_cast<size_t>(c.PointA) * 3u;
                const size_t b3 = static_cast<size_t>(c.PointB) * 3u;
                const float dx = pos[b3] - pos[a3];
                const float dy = pos[b3 + 1u] - pos[a3 + 1u];
                const float dz = pos[b3 + 2u] - pos[a3 + 2u];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist < 1.0e-6f)
                {
                    continue;
                }

                const float C          = dist - c.RestLength;
                const float kEff       = std::max(0.5f, c.Stiffness * bendMult * 3.5f);
                const float alphaTilde = invH2 / kEff;
                const float dl         = (-C - alphaTilde * body.BendDuals[k]) / (wSum + alphaTilde);
                body.BendDuals[k]     += dl;
                const float corr       = dl / dist;

                if (wa > 0.0f) { pos[a3] -= dx * corr * wa; pos[a3 + 1u] -= dy * corr * wa; pos[a3 + 2u] -= dz * corr * wa; }
                if (wb > 0.0f) { pos[b3] += dx * corr * wb; pos[b3 + 1u] += dy * corr * wb; pos[b3 + 2u] += dz * corr * wb; }
            }
        }

        // 3b. 3D Volumetric Stable Neo-Hookean FEM Tetrahedra
        if (body.TetCount > 0u)
        {
            NeoHookeanTetSolver::ProjectAllTetrahedra(body, invH2, cfg, true);
        }

        // 4. Botanical Posture / Stem Elasticity (ferns, reeds)
        if (bendMult > 0.005f && !body.PostureConstraints.empty())
        {
            for (const PostureConstraintEntry& pc : body.PostureConstraints)
            {
                if (static_cast<int32_t>(pc.PointIndex) == body.GrabbedPointIndex)
                {
                    continue;
                }
                const float w = invM[pc.PointIndex];
                if (w <= 0.0f)
                {
                    continue;
                }
                const size_t i3 = static_cast<size_t>(pc.PointIndex) * 3u;
                const float kP = pc.Stiffness * (0.25f + 1.65f * bendMult);
                const float blend = std::min(0.45f, kP * h * h * w);
                pos[i3]      += (pc.TargetX - pos[i3]) * blend;
                pos[i3 + 1u] += (pc.TargetY - pos[i3 + 1u]) * blend;
                pos[i3 + 2u] += (pc.TargetZ - pos[i3 + 2u]) * blend;
            }
        }

        // 5. Ground Plane, Interactive Sphere Collider & Jolt Rigid Proxies
        for (uint32_t i = 0u; i < n; ++i)
        {
            if (invM[i] <= 0.0f)
            {
                continue;
            }
            const size_t i3 = static_cast<size_t>(i) * 3u;
            const float floorY = body.GroundY + rad[i];
            if (pos[i3 + 1u] < floorY)
            {
                pos[i3 + 1u] = floorY;
                prev[i3]      = prev[i3] * 0.8f + pos[i3] * 0.2f;
                prev[i3 + 2u] = prev[i3 + 2u] * 0.8f + pos[i3 + 2u] * 0.2f;
            }
            if (colOn)
            {
                const float dx = pos[i3] - cx, dy = pos[i3 + 1u] - cy, dz = pos[i3 + 2u] - cz;
                const float minR = cr + rad[i];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < minR * minR && d2 > 1.0e-8f)
                {
                    const float d = std::sqrt(d2);
                    const float push = (minR - d) / d;
                    pos[i3]      += dx * push;
                    pos[i3 + 1u] += dy * push;
                    pos[i3 + 2u] += dz * push;
                }
            }
        }

        if (externalCoupling != nullptr && !externalCoupling->RigidProxies.empty())
        {
            const DeformableCouplingFeedback fb =
                DeformablePhysicsCoupling::ProjectRigidProxies(body, externalCoupling->RigidProxies);
            totalFeedback.RigidContactCount  += fb.RigidContactCount;
            totalFeedback.TotalNormalImpulse += fb.TotalNormalImpulse;
        }

        // 5b. Tire rim-barrel keep-out
        if (!body.PneumaticTires.empty() || !body.FemTires.empty())
        {
            PneumaticTireSolver::ProjectRimKeepOut(body);
        }

        // 6. XPBD Velocity Derivation
        for (uint32_t i = 0u; i < n; ++i)
        {
            const size_t i3 = static_cast<size_t>(i) * 3u;
            if (invM[i] <= 0.0f)
            {
                vel[i3] = 0.0f; vel[i3 + 1u] = 0.0f; vel[i3 + 2u] = 0.0f;
                continue;
            }
            vel[i3]      = (pos[i3] - prev[i3]) * invH;
            vel[i3 + 1u] = (pos[i3 + 1u] - prev[i3 + 1u]) * invH;
            vel[i3 + 2u] = (pos[i3 + 2u] - prev[i3 + 2u]) * invH;
        }
    }

    if (body.TetCount > 0u)
    {
        NeoHookeanTetSolver::ScatterPointStrain(body);
    }

    return totalFeedback;
}

uint32_t XpbdConstraintSolver::CutConstraintsNearSegment(
    DeformableBody& body,
    const std::array<float, 3>& segA,
    const std::array<float, 3>& segB,
    float cutRadiusMeters)
{
    uint32_t cutNow = 0u;
    const float r2 = cutRadiusMeters * cutRadiusMeters;
    const float sx = segB[0] - segA[0];
    const float sy = segB[1] - segA[1];
    const float sz = segB[2] - segA[2];
    const float sLen2 = std::max(1.0e-8f, sx * sx + sy * sy + sz * sz);

    for (DistanceConstraintEntry& c : body.DistanceConstraints)
    {
        if (c.Severed)
        {
            continue;
        }
        const size_t a3 = static_cast<size_t>(c.PointA) * 3u;
        const size_t b3 = static_cast<size_t>(c.PointB) * 3u;
        const float mx = 0.5f * (body.Positions[a3] + body.Positions[b3]);
        const float my = 0.5f * (body.Positions[a3 + 1u] + body.Positions[b3 + 1u]);
        const float mz = 0.5f * (body.Positions[a3 + 2u] + body.Positions[b3 + 2u]);

        const float t = std::clamp(((mx - segA[0]) * sx + (my - segA[1]) * sy + (mz - segA[2]) * sz) / sLen2, 0.0f, 1.0f);
        const float dx = mx - (segA[0] + sx * t);
        const float dy = my - (segA[1] + sy * t);
        const float dz = mz - (segA[2] + sz * t);
        if (dx * dx + dy * dy + dz * dz <= r2)
        {
            c.Severed = true;
            ++body.SeveredCount;
            ++cutNow;
        }
    }
    return cutNow;
}

} // namespace Frontier::SimulationSpace::Deformable
