/*============================================================================================================================================
                                                       PNEUMATICTIRESOLVER.CPP
============================================================================================================================================*/
// 🧩 Phase D03 Co-rotating pneumatic XPBD & 3D Neo-Hookean FEM tire dynamics (Boyle bulge, outward centrifugal growth, rim keep-out)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/PneumaticTireSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PNEUMATIC TIRE SOLVER
//------------------------------------------------------------------------------------------------------------------------

float PneumaticTireSolver::ComputeCavityVolume(
    const float* pos,
    uint32_t startPoint,
    uint32_t sectors,
    uint32_t profileCount,
    uint32_t layerStride,
    float hx,
    float hy,
    float hz,
    float rimRadius)
{
    float curVol = 0.0f;
    for (uint32_t si = 0u; si < sectors; ++si)
    {
        const uint32_t sNext = (si + 1u) % sectors;
        for (uint32_t pi = 0u; pi + 1u < profileCount; ++pi)
        {
            const size_t a3 = static_cast<size_t>(startPoint + (si * profileCount + pi) * layerStride) * 3u;
            const size_t b3 = static_cast<size_t>(startPoint + (sNext * profileCount + pi) * layerStride) * 3u;
            const size_t c3 = static_cast<size_t>(startPoint + (sNext * profileCount + pi + 1u) * layerStride) * 3u;
            const size_t d3 = static_cast<size_t>(startPoint + (si * profileCount + pi + 1u) * layerStride) * 3u;

            const float ux = pos[c3] - pos[a3], uy = pos[c3 + 1u] - pos[a3 + 1u], uz = pos[c3 + 2u] - pos[a3 + 2u];
            const float vx = pos[d3] - pos[b3], vy = pos[d3 + 1u] - pos[b3 + 1u], vz = pos[d3 + 2u] - pos[b3 + 2u];

            float nx = 0.5f * (uy * vz - uz * vy);
            float ny = 0.5f * (uz * vx - ux * vz);
            float nz = 0.5f * (ux * vy - uy * vx);

            const float mx = 0.25f * (pos[a3] + pos[b3] + pos[c3] + pos[d3]) - hx;
            const float my = 0.25f * (pos[a3 + 1u] + pos[b3 + 1u] + pos[c3 + 1u] + pos[d3 + 1u]) - hy;
            const float mz = 0.25f * (pos[a3 + 2u] + pos[b3 + 2u] + pos[c3 + 2u] + pos[d3 + 2u]) - hz;

            const float rLen = std::max(1.0e-6f, std::hypot(mx, my));
            const float ox = mx - (mx / rLen) * rimRadius;
            const float oy = my - (my / rLen) * rimRadius;
            if (nx * ox + ny * oy + nz * mz < 0.0f)
            {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
            curVol += (mx * nx + my * ny + mz * nz) * (1.0f / 3.0f);
        }
    }
    return curVol;
}

void PneumaticTireSolver::ApplySubstepTireForces(
    DeformableBody& body,
    float h,
    const DeformableSimulationConfiguration& cfg)
{
    float* pos  = body.Positions.data();
    float* prev = body.PrevPositions.data();
    float* vel  = body.Velocities.data();

    const bool  colOn = body.Collider.Active && cfg.ColliderEnabled;
    const float cx    = body.Collider.CenterMeters[0];
    const float cy    = body.Collider.CenterMeters[1];
    const float cz    = body.Collider.CenterMeters[2];
    const float cr    = body.Collider.RadiusMeters;

    const float spin  = cfg.TireSpinRadPerSec;
    const float spin2 = spin * spin;
    const float dA    = spin * h;
    const float cdA   = std::cos(dA);
    const float sdA   = std::sin(dA);

    // 1. Pneumatic XPBD Tires
    for (PneumaticTireCarcass& ty : body.PneumaticTires)
    {
        const uint32_t S = ty.SectorCount;
        const uint32_t P = ty.ProfileCount;
        const uint32_t base = ty.StartPointIndex;
        ty.SpinAngleRad += dA;

        // Exact co-rotating transport around axle (zero inward symplectic chord shrinkage at any RPM)
        if (std::fabs(dA) > 1.0e-9f)
        {
            const float hx0 = ty.AxleX, hy0 = ty.AxleY;
            for (uint32_t si = 0u; si < S; ++si)
            {
                for (uint32_t pi = 1u; pi + 1u < P; ++pi)
                {
                    const size_t p3 = static_cast<size_t>(base + si * P + pi) * 3u;
                    const float rx = pos[p3] - hx0, ry = pos[p3 + 1u] - hy0;
                    pos[p3]      = hx0 + rx * cdA - ry * sdA;
                    pos[p3 + 1u] = hy0 + rx * sdA + ry * cdA;
                    const float vx = vel[p3], vy = vel[p3 + 1u] - ty.AxleVelocityY;
                    vel[p3]      = vx * cdA - vy * sdA;
                    vel[p3 + 1u] = ty.AxleVelocityY + vx * sdA + vy * cdA;
                }
            }
        }

        const float curVol  = ComputeCavityVolume(pos, base, S, P, 1u, ty.AxleX, ty.AxleY, ty.AxleZ, ty.RimRadius);
        const float safeVol = std::max(ty.RestCavityVol * 0.35f, curVol);
        const float boyle   = std::clamp(ty.RestCavityVol / safeVol, 0.85f, 1.9f);

        // Outward centrifugal expansion ("dragster ballooning") inversely proportional to inflation stiffness
        const float spinStiff  = 0.65f + cfg.TirePressurePsi * 0.025f;
        const float spinGrowth = std::min(0.24f, (spin2 * 0.00048f) / spinStiff);
        const float effOuterR  = ty.OuterRadius + (ty.OuterRadius - ty.RimRadius) * spinGrowth * 1.15f;

        float supportTopY = body.GroundY;
        if (colOn)
        {
            const float dxC = ty.AxleX - cx, dzC = ty.AxleZ - cz;
            if (std::hypot(dxC, dzC) < cr + effOuterR * 0.72f)
            {
                const float under = std::max(0.0f, (cr + 0.04f) * (cr + 0.04f) - dxC * dxC * 0.45f);
                const float bumpH = cy + std::sqrt(under);
                if (bumpH > supportTopY)
                {
                    supportTopY = bumpH;
                }
            }
        }

        const float deflection = std::max(0.0f, (supportTopY + effOuterR) - ty.AxleY);
        const float kPneumatic = 18.0f + cfg.TirePressurePsi * 2.95f * boyle + spin2 * 0.08f;
        const float cPneumatic = 10.5f + cfg.TirePressurePsi * 0.20f;
        const float springUp   = (deflection > 0.0f) ? (deflection * kPneumatic - ty.AxleVelocityY * cPneumatic)
                                                     : (-ty.AxleVelocityY * 0.6f);
        const float axleForce  = -cfg.TireAxleLoad * 18.5f + std::max(0.0f, springUp)
                               - (deflection <= 0.0f ? ty.AxleVelocityY * 0.4f : 0.0f);
        ty.AxleVelocityY += axleForce * h;
        ty.AxleY         += ty.AxleVelocityY * h;
        const float minAxleY = supportTopY + ty.RimRadius + 0.05f;
        if (ty.AxleY < minAxleY)
        {
            ty.AxleY = minAxleY;
            if (ty.AxleVelocityY < 0.0f)
            {
                ty.AxleVelocityY = -ty.AxleVelocityY * 0.18f;
            }
        }

        const float psiScale = std::clamp(cfg.TirePressurePsi / 32.0f, 0.22f, 1.75f);
        const float kCarcass = (165.0f + cfg.TirePressurePsi * 14.5f) * boyle + spin2 * 0.95f;
        const float cCarcass = 18.0f + std::fabs(spin) * 1.35f;
        const float bulgeAmp = std::min(0.16f, deflection * 0.52f + std::max(0.0f, boyle - 1.0f) * 0.14f);

        for (uint32_t si = 0u; si < S; ++si)
        {
            const float a  = (static_cast<float>(si) / static_cast<float>(S)) * kTwoPi + ty.SpinAngleRad;
            const float ca = std::cos(a);
            const float sa = std::sin(a);
            const float bottomMask = std::pow(std::max(0.0f, -sa), 1.6f);

            for (uint32_t pi = 0u; pi < P; ++pi)
            {
                const size_t p3 = static_cast<size_t>(base + si * P + pi) * 3u;
                const float r0 = ty.ProfileStations[pi][0];
                const float z0 = ty.ProfileStations[pi][1];

                if (pi == 0u || pi + 1u == P)
                {
                    pos[p3]      = ty.AxleX + ca * ty.RimRadius;
                    pos[p3 + 1u] = ty.AxleY + sa * ty.RimRadius;
                    pos[p3 + 2u] = ty.AxleZ + z0;
                    prev[p3] = pos[p3]; prev[p3 + 1u] = pos[p3 + 1u]; prev[p3 + 2u] = pos[p3 + 2u];
                    continue;
                }

                const float radialArm = std::max(0.0f, r0 - ty.RimRadius);
                const float rInf      = r0 + radialArm * ((psiScale - 1.0f) * 0.045f + spinGrowth * 1.15f);
                const float sideMask  = (pi == 1u || pi == 2u || pi == 6u || pi == 7u) ? 1.0f : 0.25f;
                const float zSign     = (z0 < 0.0f) ? -1.0f : (z0 > 0.0f ? 1.0f : 0.0f);
                const float zInf      = z0 * (1.0f - spinGrowth * 0.30f)
                                      + zSign * bulgeAmp * sideMask * (0.28f + 0.72f * bottomMask);

                const float tx  = ty.AxleX + ca * rInf;
                const float tyT = ty.AxleY + sa * rInf;
                const float tz  = ty.AxleZ + zInf;

                const float centAcc = spin2 * radialArm * 0.14f;
                vel[p3]      += ((tx - pos[p3]) * kCarcass + ca * centAcc - vel[p3] * cCarcass) * h;
                vel[p3 + 1u] += ((tyT - pos[p3 + 1u]) * kCarcass + sa * centAcc + (ty.AxleVelocityY - vel[p3 + 1u]) * cCarcass) * h;
                vel[p3 + 2u] += ((tz - pos[p3 + 2u]) * kCarcass - vel[p3 + 2u] * cCarcass) * h;
            }
        }
    }

    // 2. 3D Volumetric Stable Neo-Hookean FEM Tires
    for (FemTireSolid& ft : body.FemTires)
    {
        const uint32_t S = ft.SectorCount;
        const uint32_t P = ft.ProfileCount;
        const uint32_t base = ft.StartPointIndex;
        ft.SpinAngleRad += dA;

        if (std::fabs(dA) > 1.0e-9f)
        {
            const float hx0 = ft.AxleX, hy0 = ft.AxleY;
            for (uint32_t si = 0u; si < S; ++si)
            {
                for (uint32_t pi = 1u; pi + 1u < P; ++pi)
                {
                    for (uint32_t lay = 0u; lay < 2u; ++lay)
                    {
                        const size_t p3 = static_cast<size_t>(base + (si * P + pi) * 2u + lay) * 3u;
                        const float rx = pos[p3] - hx0, ry = pos[p3 + 1u] - hy0;
                        pos[p3]      = hx0 + rx * cdA - ry * sdA;
                        pos[p3 + 1u] = hy0 + rx * sdA + ry * cdA;
                        const float vx = vel[p3], vy = vel[p3 + 1u] - ft.AxleVelocityY;
                        vel[p3]      = vx * cdA - vy * sdA;
                        vel[p3 + 1u] = ft.AxleVelocityY + vx * sdA + vy * cdA;
                    }
                }
            }
        }

        const float curVol  = ComputeCavityVolume(pos, base, S, P, 2u, ft.AxleX, ft.AxleY, ft.AxleZ, ft.RimRadius);
        const float safeVol = std::max(ft.RestCavityVol * 0.35f, curVol);
        const float boyle   = std::clamp(ft.RestCavityVol / safeVol, 0.85f, 2.0f);

        const float femStiff   = 0.55f + cfg.FemYoungMpa * 0.38f + cfg.TirePressurePsi * 0.015f;
        const float spinGrowth = std::min(0.24f, (spin2 * 0.00068f) / femStiff);
        const float effOuterR  = ft.OuterRadius + (ft.OuterRadius - ft.RimRadius) * spinGrowth * 1.25f;

        float supportTopY = body.GroundY;
        if (colOn)
        {
            const float dxC = ft.AxleX - cx, dzC = ft.AxleZ - cz;
            if (std::hypot(dxC, dzC) < cr + effOuterR * 0.72f)
            {
                const float under = std::max(0.0f, (cr + 0.04f) * (cr + 0.04f) - dxC * dxC * 0.45f);
                const float bumpH = cy + std::sqrt(under);
                if (bumpH > supportTopY)
                {
                    supportTopY = bumpH;
                }
            }
        }

        const float deflection = std::max(0.0f, (supportTopY + effOuterR) - ft.AxleY);
        const float kAxle      = 16.0f + cfg.TirePressurePsi * 2.55f * boyle + cfg.FemYoungMpa * 9.5f + spin2 * 0.09f;
        const float cAxle      = 10.5f + cfg.TirePressurePsi * 0.16f;
        const float springUp   = (deflection > 0.0f) ? (deflection * kAxle - ft.AxleVelocityY * cAxle)
                                                     : (-ft.AxleVelocityY * 0.5f);
        const float axleForce  = -cfg.TireAxleLoad * 18.5f + std::max(0.0f, springUp)
                               - (deflection <= 0.0f ? ft.AxleVelocityY * 0.35f : 0.0f);
        ft.AxleVelocityY += axleForce * h;
        ft.AxleY         += ft.AxleVelocityY * h;
        const float minAxleY = supportTopY + ft.RimRadius + 0.06f;
        if (ft.AxleY < minAxleY)
        {
            ft.AxleY = minAxleY;
            if (ft.AxleVelocityY < 0.0f)
            {
                ft.AxleVelocityY = -ft.AxleVelocityY * 0.16f;
            }
        }

        const float pressGain = (cfg.TirePressurePsi * 1.45f) * boyle;
        const float kGuide    = 46.0f + cfg.TirePressurePsi * 3.2f + spin2 * 1.15f;
        const float cGuide    = 14.0f + std::fabs(spin) * 1.65f;

        for (uint32_t si = 0u; si < S; ++si)
        {
            const float a  = (static_cast<float>(si) / static_cast<float>(S)) * kTwoPi + ft.SpinAngleRad;
            const float ca = std::cos(a);
            const float sa = std::sin(a);

            for (uint32_t pi = 0u; pi < P; ++pi)
            {
                for (uint32_t lay = 0u; lay < 2u; ++lay)
                {
                    const size_t p3 = static_cast<size_t>(base + (si * P + pi) * 2u + lay) * 3u;
                    const float r0 = (lay == 0u) ? ft.InnerStations[pi][0] : ft.OuterStations[pi][0];
                    const float z0 = (lay == 0u) ? ft.InnerStations[pi][1] : ft.OuterStations[pi][1];

                    if (pi == 0u || pi + 1u == P)
                    {
                        pos[p3]      = ft.AxleX + ca * r0;
                        pos[p3 + 1u] = ft.AxleY + sa * r0;
                        pos[p3 + 2u] = ft.AxleZ + z0;
                        prev[p3] = pos[p3]; prev[p3 + 1u] = pos[p3 + 1u]; prev[p3 + 2u] = pos[p3 + 2u];
                        continue;
                    }

                    const float radialArm = std::max(0.0f, r0 - ft.RimRadius);
                    if (lay == 0u)
                    {
                        const float ox = ca * radialArm, oy = sa * radialArm, oz = z0;
                        const float ol = std::max(1.0e-6f, std::sqrt(ox * ox + oy * oy + oz * oz));
                        vel[p3]      += (ox / ol) * pressGain * 0.28f * h;
                        vel[p3 + 1u] += (oy / ol) * pressGain * 0.28f * h;
                        vel[p3 + 2u] += (oz / ol) * pressGain * 0.28f * h;
                    }

                    const float rTarget = r0 + radialArm * spinGrowth * 1.18f;
                    const float zTarget = z0 * (1.0f - spinGrowth * cfg.FemPoissonNu * 0.45f);
                    const float tx      = ft.AxleX + ca * rTarget;
                    const float tyT     = ft.AxleY + sa * rTarget;
                    const float tz      = ft.AxleZ + zTarget;
                    const float centAcc = spin2 * radialArm * 0.14f;

                    vel[p3]      += ((tx - pos[p3]) * kGuide + ca * centAcc - vel[p3] * cGuide) * h;
                    vel[p3 + 1u] += ((tyT - pos[p3 + 1u]) * kGuide + sa * centAcc + (ft.AxleVelocityY - vel[p3 + 1u]) * cGuide) * h;
                    vel[p3 + 2u] += ((tz - pos[p3 + 2u]) * (kGuide * 0.35f) - vel[p3 + 2u] * cGuide) * h;
                }
            }
        }
    }
}

void PneumaticTireSolver::ProjectRimKeepOut(DeformableBody& body)
{
    float* pos = body.Positions.data();
    for (const PneumaticTireCarcass& ty : body.PneumaticTires)
    {
        const float minR = ty.RimRadius + 0.045f;
        for (uint32_t si = 0u; si < ty.SectorCount; ++si)
        {
            for (uint32_t pi = 1u; pi + 1u < ty.ProfileCount; ++pi)
            {
                const size_t p3 = static_cast<size_t>(ty.StartPointIndex + si * ty.ProfileCount + pi) * 3u;
                const float dx = pos[p3] - ty.AxleX, dy = pos[p3 + 1u] - ty.AxleY;
                const float d  = std::hypot(dx, dy);
                if (d < minR && d > 1.0e-6f)
                {
                    const float sc = minR / d;
                    pos[p3]      = ty.AxleX + dx * sc;
                    pos[p3 + 1u] = ty.AxleY + dy * sc;
                }
            }
        }
    }

    for (const FemTireSolid& ft : body.FemTires)
    {
        for (uint32_t si = 0u; si < ft.SectorCount; ++si)
        {
            for (uint32_t pi = 1u; pi + 1u < ft.ProfileCount; ++pi)
            {
                for (uint32_t lay = 0u; lay < 2u; ++lay)
                {
                    const float minR = ft.RimRadius + (lay == 0u ? 0.03f : 0.11f);
                    const size_t p3  = static_cast<size_t>(ft.StartPointIndex + (si * ft.ProfileCount + pi) * 2u + lay) * 3u;
                    const float dx   = pos[p3] - ft.AxleX, dy = pos[p3 + 1u] - ft.AxleY;
                    const float d    = std::hypot(dx, dy);
                    if (d < minR && d > 1.0e-6f)
                    {
                        const float sc = minR / d;
                        pos[p3]      = ft.AxleX + dx * sc;
                        pos[p3 + 1u] = ft.AxleY + dy * sc;
                    }
                }
            }
        }
    }
}

void PneumaticTireSolver::TriggerDropLift(DeformableBody& body, float liftMeters)
{
    for (PneumaticTireCarcass& ty : body.PneumaticTires)
    {
        ty.AxleY += liftMeters;
        ty.AxleVelocityY = 0.0f;
        const uint32_t end = ty.StartPointIndex + ty.SectorCount * ty.ProfileCount;
        for (uint32_t i = ty.StartPointIndex; i < end; ++i)
        {
            body.Positions[i * 3u + 1u]     += liftMeters;
            body.PrevPositions[i * 3u + 1u] += liftMeters;
            body.Velocities[i * 3u + 1u]     = 0.0f;
        }
    }
    for (FemTireSolid& ft : body.FemTires)
    {
        ft.AxleY += liftMeters;
        ft.AxleVelocityY = 0.0f;
        const uint32_t end = ft.StartPointIndex + ft.SectorCount * ft.ProfileCount * 2u;
        for (uint32_t i = ft.StartPointIndex; i < end; ++i)
        {
            body.Positions[i * 3u + 1u]     += liftMeters;
            body.PrevPositions[i * 3u + 1u] += liftMeters;
            body.Velocities[i * 3u + 1u]     = 0.0f;
        }
    }
}

} // namespace Frontier::SimulationSpace::Deformable
