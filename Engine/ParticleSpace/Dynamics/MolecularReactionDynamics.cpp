/*============================================================================================================================================
                                                      MOLECULARREACTIONDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of spatial-hashed Lennard-Jones 12-6 molecular kinetics, Langevin thermostat & A + B <-> C reaction kinetics

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/MolecularReactionDynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier::ParticleSpace
{

namespace
{

inline int64_t HashCell3D(int32_t Cx, int32_t Cy, int32_t Cz) noexcept
{
    return (static_cast<int64_t>(Cx) * 73856093LL)
         ^ (static_cast<int64_t>(Cy) * 19349663LL)
         ^ (static_cast<int64_t>(Cz) * 83492791LL);
}

inline int32_t CellCoord(float P, float InvCell) noexcept
{
    return static_cast<int32_t>(std::floor(P * InvCell));
}

inline float DeterministicNoiseSigned(uint32_t Key) noexcept
{
    uint32_t X = Key * 747796405u + 2891336453u;
    X = ((X >> ((X >> 28u) + 4u)) ^ X) * 277803737u;
    X = (X >> 22u) ^ X;
    return (static_cast<float>(X & 0xFFFFu) / 32767.5f) - 1.0f;
}

} // namespace

void StepMolecularReactionDynamics(
    const MolecularKineticsSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        ElapsedSeconds,
    float                        DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const float Sigma   = std::max(0.04f, Spec.DiameterSigma);
    const float Cutoff  = std::max(Sigma * 2.5f, Spec.ContactRadiusM * 1.6f);
    const float CutSq   = Cutoff * Cutoff;
    const float ContactSq = Spec.ContactRadiusM * Spec.ContactRadiusM;
    const float InvCell = 1.0f / Cutoff;
    const float SubDt   = std::clamp(DeltaSeconds, 0.001f, 0.025f);
    const uint32_t TimeTick = static_cast<uint32_t>(ElapsedSeconds * 1000.0f);

    std::unordered_map<int64_t, std::vector<uint32_t>> Grid;
    Grid.reserve(Slots.size());

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& S = Slots[I];
        if (!IsParticleSlotAlive(S)) continue;
        // Initialize uninitialized species deterministically as 50% A / 50% B on first visit
        if (S.AgeSeconds < 0.02f && QueryMolecularSpecies(S) == 0u && S.SeedScalar >= 0.5f)
        {
            AssignMolecularSpecies(S, MolecularSpeciesB);
        }
        const int64_t Key = HashCell3D(
            CellCoord(S.PositionMeters[0], InvCell),
            CellCoord(S.PositionMeters[1], InvCell),
            CellCoord(S.PositionMeters[2], InvCell));
        Grid[Key].push_back(I);
    }

    const float LangevinGamma = std::max(0.0f, Spec.LangevinDamping);
    const float ThermalKick   = std::sqrt(2.0f * LangevinGamma * std::max(0.01f, Spec.TemperatureKt) * SubDt);

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& A = Slots[I];
        if (!IsParticleSlotAlive(A)) continue;

        const int32_t Cx = CellCoord(A.PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(A.PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(A.PositionMeters[2], InvCell);

        float Fx = 0.0f, Fy = 0.0f, Fz = 0.0f;
        uint32_t SpA = QueryMolecularSpecies(A);

        for (int32_t Dz = -1; Dz <= 1; ++Dz)
        {
            for (int32_t Dy = -1; Dy <= 1; ++Dy)
            {
                for (int32_t Dx = -1; Dx <= 1; ++Dx)
                {
                    const auto It = Grid.find(HashCell3D(Cx + Dx, Cy + Dy, Cz + Dz));
                    if (It == Grid.end()) continue;
                    for (uint32_t J : It->second)
                    {
                        if (J <= I) continue;
                        ParticleSlot& B = Slots[J];
                        const float Rx = B.PositionMeters[0] - A.PositionMeters[0];
                        const float Ry = B.PositionMeters[1] - A.PositionMeters[1];
                        const float Rz = B.PositionMeters[2] - A.PositionMeters[2];
                        const float R2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (R2 < 1e-6f || R2 >= CutSq) continue;

                        // Regularized Lennard-Jones 12-6 pair force: F = 24*eps/r * (2*(s/r)^12 - (s/r)^6)
                        const float SafeR2 = std::max(R2, (Sigma * 0.65f) * (Sigma * 0.65f));
                        const float Sr2    = (Sigma * Sigma) / SafeR2;
                        const float Sr6    = Sr2 * Sr2 * Sr2;
                        const float ForceScalar = std::clamp(24.0f * Spec.WellDepthEpsilon * (2.0f * Sr6 * Sr6 - Sr6) / SafeR2, -35.0f, 35.0f);

                        // Repulsive pushes A away from B (-r), attractive pulls A toward B (+r)
                        Fx -= Rx * ForceScalar;
                        Fy -= Ry * ForceScalar;
                        Fz -= Rz * ForceScalar;
                        B.VelocityMps[0] += Rx * ForceScalar * SubDt;
                        B.VelocityMps[1] += Ry * ForceScalar * SubDt;
                        B.VelocityMps[2] += Rz * ForceScalar * SubDt;

                        // Reversible chemical reaction A + B -> C when within ContactRadiusM
                        if (Spec.ChemicalReaction && R2 < ContactSq)
                        {
                            const uint32_t SpB = QueryMolecularSpecies(B);
                            const bool OppositeReactants = (SpA == MolecularSpeciesA && SpB == MolecularSpeciesB)
                                                        || (SpA == MolecularSpeciesB && SpB == MolecularSpeciesA);
                            if (OppositeReactants)
                            {
                                const float Prob = std::clamp(Spec.ForwardRate * SubDt, 0.0f, 1.0f);
                                const float Roll = 0.5f + 0.5f * DeterministicNoiseSigned(I * 1973u + J * 9277u + TimeTick);
                                if (Roll < Prob)
                                {
                                    AssignMolecularSpecies(A, MolecularSpeciesC);
                                    AssignMolecularSpecies(B, MolecularSpeciesC);
                                    SpA = MolecularSpeciesC;
                                    // Exothermic binding impulse
                                    A.VelocityMps[1] += 0.35f;
                                    B.VelocityMps[1] += 0.35f;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Reversible dissociation C -> A or B
        if (Spec.ChemicalReaction && SpA == MolecularSpeciesC)
        {
            const float DissProb = std::clamp(Spec.DissociationRate * SubDt, 0.0f, 1.0f);
            const float Roll     = 0.5f + 0.5f * DeterministicNoiseSigned(I * 4099u + TimeTick * 17u);
            if (Roll < DissProb)
            {
                SpA = (A.SeedScalar < 0.5f) ? MolecularSpeciesA : MolecularSpeciesB;
                AssignMolecularSpecies(A, SpA);
            }
        }

        // Langevin thermostat damping + thermal Gaussian-like jitter
        const float Damp = std::exp(-LangevinGamma * SubDt);
        A.VelocityMps[0] = (A.VelocityMps[0] + Fx * SubDt) * Damp + ThermalKick * DeterministicNoiseSigned(I * 13u + TimeTick);
        A.VelocityMps[1] = (A.VelocityMps[1] + Fy * SubDt) * Damp + ThermalKick * DeterministicNoiseSigned(I * 29u + TimeTick + 1u);
        A.VelocityMps[2] = (A.VelocityMps[2] + Fz * SubDt) * Damp + ThermalKick * DeterministicNoiseSigned(I * 53u + TimeTick + 2u);

        // Soft reaction vessel box around OriginCenter
        const float BoxHalf = 2.2f;
        for (int K = 0; K < 3; ++K)
        {
            const float MinB = OriginCenter[K] - BoxHalf;
            const float MaxB = OriginCenter[K] + BoxHalf;
            if (A.PositionMeters[K] < MinB) { A.PositionMeters[K] = MinB; A.VelocityMps[K] = std::fabs(A.VelocityMps[K]) * 0.85f; }
            if (A.PositionMeters[K] > MaxB) { A.PositionMeters[K] = MaxB; A.VelocityMps[K] = -std::fabs(A.VelocityMps[K]) * 0.85f; }
        }

        A.AuxScalar = static_cast<float>(SpA) * 0.5f; // 0.0=A, 0.5=B, 1.0=C
    }
}

} // namespace Frontier::ParticleSpace
