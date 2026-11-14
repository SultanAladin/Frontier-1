/*============================================================================================================================================
                                                         CURLMANIFOLDDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of divergence-free 3D curl noise & manifold projection solvers (modes 1..5)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/CurlManifoldDynamics.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void EvaluateDivergenceFreeCurl(
    float X,
    float Y,
    float Z,
    float FrequencyPerM,
    float TimePhase,
    float OutVelocity[3]) noexcept
{
    // Exact analytic curl of smooth 3D trigonometric stream-potential Psi = (PsiX, PsiY, PsiZ)
    // Guarantees div(OutVelocity) == 0 everywhere in R^3.
    const float F  = std::max(0.05f, FrequencyPerM);
    const float Sx = X * F + TimePhase;
    const float Sy = Y * F - TimePhase * 0.73f;
    const float Sz = Z * F + TimePhase * 0.51f;

    // Two-octave divergence-free harmonic stream potential
    const float Sx2 = X * (F * 2.03f) - TimePhase * 1.17f;
    const float Sy2 = Y * (F * 2.03f) + TimePhase * 0.89f;
    const float Sz2 = Z * (F * 2.03f) - TimePhase * 0.61f;

    // Curl_x = dPsiZ/dy - dPsiY/dz, with PsiX=sin(Sy)cos(Sz), PsiY=sin(Sz)cos(Sx), PsiZ=sin(Sx)cos(Sy)
    const float C1x = -std::sin(Sx) * std::sin(Sy) - std::cos(Sz) * std::cos(Sx);
    const float C1y = -std::sin(Sy) * std::sin(Sz) - std::cos(Sx) * std::cos(Sy);
    const float C1z = -std::sin(Sz) * std::sin(Sx) - std::cos(Sy) * std::cos(Sz);

    const float C2x = -std::sin(Sx2) * std::sin(Sy2) - std::cos(Sz2) * std::cos(Sx2);
    const float C2y = -std::sin(Sy2) * std::sin(Sz2) - std::cos(Sx2) * std::cos(Sy2);
    const float C2z = -std::sin(Sz2) * std::sin(Sx2) - std::cos(Sy2) * std::cos(Sz2);

    OutVelocity[0] = C1x * 0.72f + C2x * 0.28f;
    OutVelocity[1] = C1y * 0.72f + C2y * 0.28f;
    OutVelocity[2] = C1z * 0.72f + C2z * 0.28f;
}

void StepCurlManifoldDynamics(
    const CurlManifoldSpec& Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept
{
    if (Spec.ManifoldMode == 0u || DeltaSeconds <= 0.0f) return;

    const float Phase    = ElapsedSeconds * Spec.EvolutionRateHz;
    const float ShellR   = std::max(0.25f, Spec.ShellRadiusM);
    const float TubeR    = std::max(0.15f, Spec.TubeRadiusM);
    const float Gain     = Spec.DriveSpeedMps * DeltaSeconds;

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        float CurlV[3] {};
        EvaluateDivergenceFreeCurl(
            Slot.PositionMeters[0],
            Slot.PositionMeters[1],
            Slot.PositionMeters[2],
            Spec.FrequencyPerM,
            Phase,
            CurlV);

        const float Rx = Slot.PositionMeters[0] - OriginCenter[0];
        const float Ry = Slot.PositionMeters[1] - OriginCenter[1];
        const float Rz = Slot.PositionMeters[2] - OriginCenter[2];

        if (Spec.ManifoldMode == 1u)
        {
            // Mode 1: Free 3D curl advection
            Slot.VelocityMps[0] += CurlV[0] * Gain;
            Slot.VelocityMps[1] += CurlV[1] * Gain;
            Slot.VelocityMps[2] += CurlV[2] * Gain;
        }
        else if (Spec.ManifoldMode == 2u)
        {
            // Mode 2: Soft radial tether + 3D curl silk
            const float Dist = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz) + 1e-5f;
            const float Pull = (Dist > ShellR) ? (-(Dist - ShellR) * 2.4f * DeltaSeconds / Dist) : 0.0f;
            Slot.VelocityMps[0] += CurlV[0] * Gain + Rx * Pull;
            Slot.VelocityMps[1] += CurlV[1] * Gain + Ry * Pull;
            Slot.VelocityMps[2] += CurlV[2] * Gain + Rz * Pull;
        }
        else if (Spec.ManifoldMode == 3u)
        {
            // Mode 3: Spherical shell projection — project curl tangent to sphere surface
            const float Dist = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz) + 1e-5f;
            const float Nx = Rx / Dist, Ny = Ry / Dist, Nz = Rz / Dist;
            const float RadDot = CurlV[0] * Nx + CurlV[1] * Ny + CurlV[2] * Nz;
            const float Tx = CurlV[0] - Nx * RadDot;
            const float Ty = CurlV[1] - Ny * RadDot;
            const float Tz = CurlV[2] - Nz * RadDot;
            const float Restore = (ShellR - Dist) * 5.5f * DeltaSeconds;
            Slot.VelocityMps[0] += Tx * Gain + Nx * Restore;
            Slot.VelocityMps[1] += Ty * Gain + Ny * Restore;
            Slot.VelocityMps[2] += Tz * Gain + Nz * Restore;
        }
        else if (Spec.ManifoldMode == 4u)
        {
            // Mode 4: Toroidal loop surface flow around major radius ShellR and tube radius TubeR
            const float RadialXZ = std::sqrt(Rx * Rx + Rz * Rz) + 1e-5f;
            const float RingX    = (Rx / RadialXZ) * ShellR;
            const float RingZ    = (Rz / RadialXZ) * ShellR;
            const float Dx       = Rx - RingX;
            const float Dy       = Ry;
            const float Dz       = Rz - RingZ;
            const float TDist    = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) + 1e-5f;
            const float Restore  = (TubeR - TDist) * 4.8f * DeltaSeconds / TDist;
            // Tangential swirl around torus major ring + curl turbulence
            const float SwirlX   = (-Rz / RadialXZ) * (Spec.DriveSpeedMps * 0.45f) * DeltaSeconds;
            const float SwirlZ   = ( Rx / RadialXZ) * (Spec.DriveSpeedMps * 0.45f) * DeltaSeconds;
            Slot.VelocityMps[0] += CurlV[0] * Gain * 0.65f + Dx * Restore + SwirlX;
            Slot.VelocityMps[1] += CurlV[1] * Gain * 0.65f + Dy * Restore;
            Slot.VelocityMps[2] += CurlV[2] * Gain * 0.65f + Dz * Restore + SwirlZ;
        }
        else
        {
            // Mode 5: Vortex funnel spiral updraft
            const float RadialXZ = std::sqrt(Rx * Rx + Rz * Rz) + 1e-5f;
            const float FunnelR  = std::max(0.22f, 0.35f + std::max(0.0f, Ry) * 0.38f);
            const float PullXZ   = (FunnelR - RadialXZ) * 3.8f * DeltaSeconds / RadialXZ;
            const float TangSpd  = (Spec.DriveSpeedMps * 1.15f / (0.45f + RadialXZ)) * DeltaSeconds;
            Slot.VelocityMps[0] += (-Rz / RadialXZ) * TangSpd + Rx * PullXZ + CurlV[0] * Gain * 0.4f;
            Slot.VelocityMps[1] += (1.85f * DeltaSeconds) + CurlV[1] * Gain * 0.4f;
            Slot.VelocityMps[2] += ( Rx / RadialXZ) * TangSpd + Rz * PullXZ + CurlV[2] * Gain * 0.4f;
        }
    }
}

} // namespace Frontier::ParticleSpace
