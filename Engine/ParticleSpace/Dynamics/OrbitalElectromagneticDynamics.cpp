/*============================================================================================================================================
                                                   ORBITALELECTROMAGNETICDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of Keplerian gravity, Boris electromagnetic pusher, Chladni standing waves & 9 RK4 chaotic attractors

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/OrbitalElectromagneticDynamics.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float Pi = 3.14159265358979323846f;

} // namespace

void EvaluateStrangeAttractorDerivative(
    uint8_t AttractorKind,
    float   X,
    float   Y,
    float   Z,
    float   OutDeriv[3]) noexcept
{
    switch (AttractorKind % 9u)
    {
        case 0u: // Lorenz
            OutDeriv[0] = 10.0f * (Y - X);
            OutDeriv[1] = X * (28.0f - Z) - Y;
            OutDeriv[2] = X * Y - (8.0f / 3.0f) * Z;
            break;

        case 1u: // Thomas cyclically symmetric
        {
            constexpr float B = 0.19f;
            OutDeriv[0] = std::sin(Y) - B * X;
            OutDeriv[1] = std::sin(Z) - B * Y;
            OutDeriv[2] = std::sin(X) - B * Z;
            break;
        }

        case 2u: // Aizawa
        {
            constexpr float A = 0.95f, B = 0.7f, C = 0.6f, D = 3.5f, E = 0.25f, F = 0.1f;
            OutDeriv[0] = (Z - B) * X - D * Y;
            OutDeriv[1] = D * X + (Z - B) * Y;
            OutDeriv[2] = C + A * Z - (Z * Z * Z) / 3.0f - (X * X + Y * Y) * (1.0f + E * Z) + F * Z * X * X * X;
            break;
        }

        case 3u: // Halvorsen
        {
            constexpr float A = 1.4f;
            OutDeriv[0] = -A * X - 4.0f * Y - 4.0f * Z - Y * Y;
            OutDeriv[1] = -A * Y - 4.0f * Z - 4.0f * X - Z * Z;
            OutDeriv[2] = -A * Z - 4.0f * X - 4.0f * Y - X * X;
            break;
        }

        case 4u: // Dadras
        {
            constexpr float A = 3.0f, B = 2.7f, C = 1.7f, D = 2.0f, E = 9.0f;
            OutDeriv[0] = Y - A * X + B * Y * Z;
            OutDeriv[1] = C * Y - X * Z + Z;
            OutDeriv[2] = D * X * Y - E * Z;
            break;
        }

        case 5u: // Chen
            OutDeriv[0] = 35.0f * (Y - X);
            OutDeriv[1] = -7.0f * X - X * Z + 28.0f * Y;
            OutDeriv[2] = X * Y - 3.0f * Z;
            break;

        case 6u: // Rossler
            OutDeriv[0] = -(Y + Z);
            OutDeriv[1] = X + 0.2f * Y;
            OutDeriv[2] = 0.2f + Z * (X - 5.7f);
            break;

        case 7u: // Sprott B
            OutDeriv[0] = Y * Z;
            OutDeriv[1] = X - Y;
            OutDeriv[2] = 1.0f - X * Y;
            break;

        default: // 8: Four-Wing
            OutDeriv[0] = 0.2f * X + Y * Z;
            OutDeriv[1] = 0.01f * X - 0.4f * Y - X * Z;
            OutDeriv[2] = -Z - X * Y;
            break;
    }
}

void StepOrbitalGravityDynamics(
    const OrbitalGravitySpec& Spec,
    const float               OriginCenter[3],
    std::span<ParticleSlot>   Slots,
    float                     DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float SoftSq = Spec.SofteningMeters * Spec.SofteningMeters + 1e-5f;
    const float Mu     = Spec.CentralMassParam;

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        const float Rx    = Slot.PositionMeters[0] - OriginCenter[0];
        const float Ry    = Slot.PositionMeters[1] - OriginCenter[1];
        const float Rz    = Slot.PositionMeters[2] - OriginCenter[2];
        const float R2    = Rx * Rx + Ry * Ry + Rz * Rz + SoftSq;
        const float InvR  = 1.0f / std::sqrt(R2);
        const float InvR3 = InvR / R2;

        // Central Plummer-softened Newtonian gravity
        const float AccScale = -Mu * InvR3 * DeltaSeconds;
        Slot.VelocityMps[0] += Rx * AccScale;
        Slot.VelocityMps[1] += Ry * AccScale;
        Slot.VelocityMps[2] += Rz * AccScale;

        // Subtle disk-plane stabilization & tangential swirl when TangentialBoost > 0
        if (Spec.TangentialBoost > 1e-4f)
        {
            const float RadXZ = std::sqrt(Rx * Rx + Rz * Rz) + 1e-4f;
            const float Tang  = Spec.TangentialBoost * DeltaSeconds * InvR;
            Slot.VelocityMps[0] += (-Rz / RadXZ) * Tang;
            Slot.VelocityMps[2] += ( Rx / RadXZ) * Tang;
        }
    }

    // Optional bounded pairwise N-body mutual attraction (capped at 128 active attractors for real-time determinism)
    if (Spec.EnablePairwiseNBody)
    {
        const std::size_t MaxBodies = std::min<std::size_t>(Slots.size(), 128u);
        const float PairMu = Mu * 0.015f * DeltaSeconds;
        for (std::size_t I = 0u; I < MaxBodies; ++I)
        {
            if (!IsParticleSlotAlive(Slots[I])) continue;
            for (std::size_t J = I + 1u; J < MaxBodies; ++J)
            {
                if (!IsParticleSlotAlive(Slots[J])) continue;
                const float Dx = Slots[J].PositionMeters[0] - Slots[I].PositionMeters[0];
                const float Dy = Slots[J].PositionMeters[1] - Slots[I].PositionMeters[1];
                const float Dz = Slots[J].PositionMeters[2] - Slots[I].PositionMeters[2];
                const float D2 = Dx * Dx + Dy * Dy + Dz * Dz + SoftSq;
                const float F  = PairMu / (D2 * std::sqrt(D2));
                Slots[I].VelocityMps[0] += Dx * F;
                Slots[I].VelocityMps[1] += Dy * F;
                Slots[I].VelocityMps[2] += Dz * F;
                Slots[J].VelocityMps[0] -= Dx * F;
                Slots[J].VelocityMps[1] -= Dy * F;
                Slots[J].VelocityMps[2] -= Dz * F;
            }
        }
    }
}

void StepElectromagneticDynamics(
    const ElectromagneticSpec& Spec,
    const float                OriginCenter[3],
    std::span<ParticleSlot>    Slots,
    float                      DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float HalfQOverM = 0.5f * Spec.ChargeToMassRatio * DeltaSeconds;

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        // Alternate charge polarity by SeedScalar when plasma has mixed electrons/ions
        const float Sign = (Slot.SeedScalar < 0.5f) ? 1.0f : -1.0f;
        const float Q    = HalfQOverM * Sign;

        // Compute local magnetic field B = B_uniform + B_dipole(r)
        float Bx = Spec.MagneticFieldB[0];
        float By = Spec.MagneticFieldB[1];
        float Bz = Spec.MagneticFieldB[2];

        if (std::fabs(Spec.DipoleMoment) > 1e-4f)
        {
            const float Rx  = Slot.PositionMeters[0] - OriginCenter[0];
            const float Ry  = Slot.PositionMeters[1] - OriginCenter[1];
            const float Rz  = Slot.PositionMeters[2] - OriginCenter[2];
            const float R2  = Rx * Rx + Ry * Ry + Rz * Rz + 0.12f;
            const float InvR = 1.0f / std::sqrt(R2);
            const float InvR3 = InvR / R2;
            const float Rhx = Rx * InvR, Rhy = Ry * InvR, Rhz = Rz * InvR;
            // Magnetic dipole along +Y: B = m * (3 (m_hat . r_hat) r_hat - m_hat) / r^3
            const float MdotR = Rhy;
            const float Scale = Spec.DipoleMoment * InvR3;
            Bx += Scale * (3.0f * MdotR * Rhx);
            By += Scale * (3.0f * MdotR * Rhy - 1.0f);
            Bz += Scale * (3.0f * MdotR * Rhz);
        }

        // Exact energy-conserving Boris algorithm for Lorentz force F = q (E + v x B)
        const float VmX = Slot.VelocityMps[0] + Q * Spec.ElectricFieldE[0];
        const float VmY = Slot.VelocityMps[1] + Q * Spec.ElectricFieldE[1];
        const float VmZ = Slot.VelocityMps[2] + Q * Spec.ElectricFieldE[2];

        const float Tx = Q * Bx, Ty = Q * By, Tz = Q * Bz;
        const float T2 = Tx * Tx + Ty * Ty + Tz * Tz;
        const float SFac = 2.0f / (1.0f + T2);
        const float Sx = Tx * SFac, Sy = Ty * SFac, Sz = Tz * SFac;

        // v' = v^- + v^- x t
        const float VpX = VmX + (VmY * Tz - VmZ * Ty);
        const float VpY = VmY + (VmZ * Tx - VmX * Tz);
        const float VpZ = VmZ + (VmX * Ty - VmY * Tx);

        // v^+ = v^- + v' x s
        const float VplusX = VmX + (VpY * Sz - VpZ * Sy);
        const float VplusY = VmY + (VpZ * Sx - VpX * Sz);
        const float VplusZ = VmZ + (VpX * Sy - VpY * Sx);

        Slot.VelocityMps[0] = VplusX + Q * Spec.ElectricFieldE[0];
        Slot.VelocityMps[1] = VplusY + Q * Spec.ElectricFieldE[1];
        Slot.VelocityMps[2] = VplusZ + Q * Spec.ElectricFieldE[2];
    }
}

void StepHarmonicWaveDynamics(
    const HarmonicWaveSpec& Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float M     = std::max(1.0f, static_cast<float>(Spec.ModeIndexM));
    const float N     = std::max(1.0f, static_cast<float>(Spec.ModeIndexN));
    const float Plate = 3.2f;
    const float Pulse = 1.0f + 0.18f * std::sin(ElapsedSeconds * Spec.FrequencyHz * 2.0f * Pi);

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        const float U = ((Slot.PositionMeters[0] - OriginCenter[0]) / Plate) * Pi;
        const float V = ((Slot.PositionMeters[2] - OriginCenter[2]) / Plate) * Pi;

        // Chladni standing-wave potential Phi(u,v) = cos(m u) cos(n v) - cos(n u) cos(m v)
        const float Phi   = std::cos(M * U) * std::cos(N * V) - std::cos(N * U) * std::cos(M * V);
        const float dPhiU = -M * std::sin(M * U) * std::cos(N * V) + N * std::sin(N * U) * std::cos(M * V);
        const float dPhiV = -N * std::cos(M * U) * std::sin(N * V) + M * std::cos(N * U) * std::sin(M * V);

        // Gradient descent onto nodal line Phi == 0 (minimizing 0.5 * Phi^2)
        const float Gain = -Spec.AmplitudeMeters * Pulse * 2.6f * DeltaSeconds;
        Slot.VelocityMps[0] += Phi * dPhiU * Gain;
        Slot.VelocityMps[2] += Phi * dPhiV * Gain;

        // Gentle vertical acoustic resonance elevation
        const float TargetY = OriginCenter[1] + std::fabs(Phi) * Spec.AmplitudeMeters * 0.42f * Pulse;
        Slot.VelocityMps[1] += (TargetY - Slot.PositionMeters[1]) * 6.0f * DeltaSeconds;
        Slot.AuxScalar       = std::clamp(1.0f - std::fabs(Phi) * 0.65f, 0.0f, 1.0f);
    }
}

void StepStrangeAttractorDynamics(
    const AttractorDynamicsSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float InvScale = 1.0f / std::max(0.01f, Spec.ScaleFactor);
    const float StepH    = std::clamp(DeltaSeconds * Spec.TimeMultiplier, 0.0005f, 0.025f);

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        const float X0 = (Slot.PositionMeters[0] - OriginCenter[0]) * InvScale;
        const float Y0 = (Slot.PositionMeters[1] - OriginCenter[1]) * InvScale;
        const float Z0 = (Slot.PositionMeters[2] - OriginCenter[2]) * InvScale;

        float K1[3], K2[3], K3[3], K4[3];
        EvaluateStrangeAttractorDerivative(Spec.AttractorKind, X0, Y0, Z0, K1);
        EvaluateStrangeAttractorDerivative(Spec.AttractorKind, X0 + 0.5f * StepH * K1[0], Y0 + 0.5f * StepH * K1[1], Z0 + 0.5f * StepH * K1[2], K2);
        EvaluateStrangeAttractorDerivative(Spec.AttractorKind, X0 + 0.5f * StepH * K2[0], Y0 + 0.5f * StepH * K2[1], Z0 + 0.5f * StepH * K2[2], K3);
        EvaluateStrangeAttractorDerivative(Spec.AttractorKind, X0 + StepH * K3[0], Y0 + StepH * K3[1], Z0 + StepH * K3[2], K4);

        const float Vx = ((K1[0] + 2.0f * K2[0] + 2.0f * K3[0] + K4[0]) / 6.0f) * Spec.ScaleFactor * Spec.TimeMultiplier;
        const float Vy = ((K1[1] + 2.0f * K2[1] + 2.0f * K3[1] + K4[1]) / 6.0f) * Spec.ScaleFactor * Spec.TimeMultiplier;
        const float Vz = ((K1[2] + 2.0f * K2[2] + 2.0f * K3[2] + K4[2]) / 6.0f) * Spec.ScaleFactor * Spec.TimeMultiplier;

        // Guard against unbounded divergence on far-out initial seeds
        const float Spd = std::sqrt(Vx * Vx + Vy * Vy + Vz * Vz);
        const float Cap = (Spd > 32.0f) ? (32.0f / Spd) : 1.0f;
        Slot.VelocityMps[0] = Vx * Cap;
        Slot.VelocityMps[1] = Vy * Cap;
        Slot.VelocityMps[2] = Vz * Cap;
    }
}

} // namespace Frontier::ParticleSpace
