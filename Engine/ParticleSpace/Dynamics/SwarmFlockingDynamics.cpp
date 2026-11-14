/*============================================================================================================================================
                                                        SWARMFLOCKINGDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of spatial-hashed Reynolds Boids, multi-species Particle Life, & Physarum chemotaxis dynamics

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SwarmFlockingDynamics.h"

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

} // namespace

void StepSwarmFlockingDynamics(
    const SwarmFlockingSpec& Spec,
    const float              OriginCenter[3],
    std::span<ParticleSlot>  Slots,
    float                    DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const float PercR   = std::max(0.25f, Spec.PerceptionRadiusM);
    const float PercSq  = PercR * PercR;
    const float SepR    = PercR * 0.42f;
    const float SepSq   = SepR * SepR;
    const float InvCell = 1.0f / PercR;

    // Spatial hash bucket table for O(N) neighbor lookup
    std::unordered_map<int64_t, std::vector<uint32_t>> Grid;
    Grid.reserve(Slots.size());

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        if (!IsParticleSlotAlive(Slots[I])) continue;
        const int64_t Key = HashCell3D(
            CellCoord(Slots[I].PositionMeters[0], InvCell),
            CellCoord(Slots[I].PositionMeters[1], InvCell),
            CellCoord(Slots[I].PositionMeters[2], InvCell));
        Grid[Key].push_back(I);
    }

    const float MaxSpd = std::max(0.5f, Spec.MaxSpeedMps);
    const float MinSpd = MaxSpd * 0.25f;

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& Self = Slots[I];
        if (!IsParticleSlotAlive(Self)) continue;

        const int32_t Cx = CellCoord(Self.PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(Self.PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(Self.PositionMeters[2], InvCell);

        float SepX = 0.0f, SepY = 0.0f, SepZ = 0.0f;
        float AliX = 0.0f, AliY = 0.0f, AliZ = 0.0f;
        float CohX = 0.0f, CohY = 0.0f, CohZ = 0.0f;
        uint32_t NeighborCount = 0u;

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
                        if (J == I || NeighborCount >= 24u) continue;
                        const ParticleSlot& Other = Slots[J];
                        const float Rx = Other.PositionMeters[0] - Self.PositionMeters[0];
                        const float Ry = Other.PositionMeters[1] - Self.PositionMeters[1];
                        const float Rz = Other.PositionMeters[2] - Self.PositionMeters[2];
                        const float D2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (D2 < 1e-6f || D2 > PercSq) continue;

                        if (D2 < SepSq)
                        {
                            const float InvD = 1.0f / (D2 + 0.02f);
                            SepX -= Rx * InvD;
                            SepY -= Ry * InvD;
                            SepZ -= Rz * InvD;
                        }
                        AliX += Other.VelocityMps[0];
                        AliY += Other.VelocityMps[1];
                        AliZ += Other.VelocityMps[2];
                        CohX += Other.PositionMeters[0];
                        CohY += Other.PositionMeters[1];
                        CohZ += Other.PositionMeters[2];
                        ++NeighborCount;
                    }
                }
            }
        }

        float SteerX = 0.0f, SteerY = 0.0f, SteerZ = 0.0f;
        if (NeighborCount > 0u)
        {
            const float InvN = 1.0f / static_cast<float>(NeighborCount);
            SteerX += SepX * Spec.SeparationWeight
                    + (AliX * InvN - Self.VelocityMps[0]) * Spec.AlignmentWeight
                    + (CohX * InvN - Self.PositionMeters[0]) * Spec.CohesionWeight;
            SteerY += SepY * Spec.SeparationWeight
                    + (AliY * InvN - Self.VelocityMps[1]) * Spec.AlignmentWeight
                    + (CohY * InvN - Self.PositionMeters[1]) * Spec.CohesionWeight;
            SteerZ += SepZ * Spec.SeparationWeight
                    + (AliZ * InvN - Self.VelocityMps[2]) * Spec.AlignmentWeight
                    + (CohZ * InvN - Self.PositionMeters[2]) * Spec.CohesionWeight;
        }

        // Soft home boundary containment around OriginCenter
        const float Bx = Self.PositionMeters[0] - OriginCenter[0];
        const float By = Self.PositionMeters[1] - OriginCenter[1];
        const float Bz = Self.PositionMeters[2] - OriginCenter[2];
        const float BDist = std::sqrt(Bx * Bx + By * By + Bz * Bz);
        if (BDist > 4.2f)
        {
            const float Pull = (BDist - 4.2f) * 2.2f / BDist;
            SteerX -= Bx * Pull;
            SteerY -= By * Pull;
            SteerZ -= Bz * Pull;
        }

        Self.VelocityMps[0] += SteerX * DeltaSeconds;
        Self.VelocityMps[1] += SteerY * DeltaSeconds;
        Self.VelocityMps[2] += SteerZ * DeltaSeconds;

        const float Spd = std::sqrt(
            Self.VelocityMps[0] * Self.VelocityMps[0]
          + Self.VelocityMps[1] * Self.VelocityMps[1]
          + Self.VelocityMps[2] * Self.VelocityMps[2]);
        if (Spd > MaxSpd)
        {
            const float S = MaxSpd / Spd;
            Self.VelocityMps[0] *= S; Self.VelocityMps[1] *= S; Self.VelocityMps[2] *= S;
        }
        else if (Spd > 1e-4f && Spd < MinSpd)
        {
            const float S = MinSpd / Spd;
            Self.VelocityMps[0] *= S; Self.VelocityMps[1] *= S; Self.VelocityMps[2] *= S;
        }
    }
}

void StepSpeciesLifeDynamics(
    const SpeciesLifeSpec&  Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const uint32_t SpeciesN = std::clamp<uint32_t>(Spec.SpeciesCount, 2u, 6u);
    const float    RMax     = std::max(0.35f, Spec.InteractionRadiusM);
    const float    RMaxSq   = RMax * RMax;
    const float    Beta     = 0.30f; // Core short-range repulsion threshold
    const float    InvCell  = 1.0f / RMax;

    // Deterministic cyclic predator-prey + symbiosis affinity matrix in [-1, +1]
    auto Affinity = [SpeciesN](uint32_t A, uint32_t B) noexcept -> float {
        if (A == B) return 0.35f;
        if ((A + 1u) % SpeciesN == B) return 0.85f;
        if ((B + 1u) % SpeciesN == A) return -0.65f;
        return (((A * 7u + B * 13u) % 5u) - 2.0f) * 0.25f;
    };

    std::unordered_map<int64_t, std::vector<uint32_t>> Grid;
    Grid.reserve(Slots.size());
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        if (!IsParticleSlotAlive(Slots[I])) continue;
        const int64_t Key = HashCell3D(
            CellCoord(Slots[I].PositionMeters[0], InvCell),
            CellCoord(Slots[I].PositionMeters[1], InvCell),
            CellCoord(Slots[I].PositionMeters[2], InvCell));
        Grid[Key].push_back(I);
    }

    const float Friction = std::clamp(Spec.FrictionDecay, 0.5f, 0.995f);

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& Self = Slots[I];
        if (!IsParticleSlotAlive(Self)) continue;

        const uint32_t SpA = static_cast<uint32_t>(Self.SeedScalar * static_cast<float>(SpeciesN)) % SpeciesN;
        Self.AuxScalar     = static_cast<float>(SpA) / static_cast<float>(SpeciesN - 1u);

        const int32_t Cx = CellCoord(Self.PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(Self.PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(Self.PositionMeters[2], InvCell);

        float Fx = 0.0f, Fy = 0.0f, Fz = 0.0f;
        uint32_t Checked = 0u;

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
                        if (J == I || Checked >= 28u) continue;
                        const ParticleSlot& Other = Slots[J];
                        const float Rx = Other.PositionMeters[0] - Self.PositionMeters[0];
                        const float Ry = Other.PositionMeters[1] - Self.PositionMeters[1];
                        const float Rz = Other.PositionMeters[2] - Self.PositionMeters[2];
                        const float D2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (D2 < 1e-6f || D2 > RMaxSq) continue;

                        const float D   = std::sqrt(D2);
                        const float Rn  = D / RMax;
                        const uint32_t SpB = static_cast<uint32_t>(Other.SeedScalar * static_cast<float>(SpeciesN)) % SpeciesN;
                        float ForceMag = 0.0f;
                        if (Rn < Beta)
                        {
                            ForceMag = (Rn / Beta) - 1.0f;
                        }
                        else
                        {
                            const float Aff = Affinity(SpA, SpB);
                            ForceMag = Aff * (1.0f - std::fabs(2.0f * Rn - 1.0f - Beta) / (1.0f - Beta));
                        }
                        const float Scale = ForceMag * Spec.ForceMultiplier / D;
                        Fx += Rx * Scale;
                        Fy += Ry * Scale;
                        Fz += Rz * Scale;
                        ++Checked;
                    }
                }
            }
        }

        // Soft containment restoring force
        const float Bx = Self.PositionMeters[0] - OriginCenter[0];
        const float By = Self.PositionMeters[1] - OriginCenter[1];
        const float Bz = Self.PositionMeters[2] - OriginCenter[2];
        const float BDist = std::sqrt(Bx * Bx + By * By + Bz * Bz);
        if (BDist > 3.6f)
        {
            const float Pull = (BDist - 3.6f) * 3.0f / BDist;
            Fx -= Bx * Pull; Fy -= By * Pull; Fz -= Bz * Pull;
        }

        Self.VelocityMps[0] = (Self.VelocityMps[0] + Fx * DeltaSeconds) * Friction;
        Self.VelocityMps[1] = (Self.VelocityMps[1] + Fy * DeltaSeconds) * Friction;
        Self.VelocityMps[2] = (Self.VelocityMps[2] + Fz * DeltaSeconds) * Friction;
    }
}

void StepMorphogeneticGrowthDynamics(
    const MorphogeneticGrowthSpec& Spec,
    const float                    OriginCenter[3],
    std::span<ParticleSlot>        Slots,
    float                          ElapsedSeconds,
    float                          DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const float SenseDist = std::max(0.15f, Spec.SensorDistanceM);
    const float TurnStep  = Spec.TurnRateRadPerSec * DeltaSeconds;
    const float Speed     = 1.35f;

    // Analytic nutrient / chemoattractant field mimicking slime-mold network ridges
    auto SampleScent = [ElapsedSeconds, OriginCenter](float X, float Y, float Z) noexcept -> float {
        const float Rx = X - OriginCenter[0];
        const float Ry = Y - OriginCenter[1];
        const float Rz = Z - OriginCenter[2];
        const float R  = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
        const float Network = std::sin(Rx * 2.4f + ElapsedSeconds * 0.25f) * std::cos(Rz * 2.4f - ElapsedSeconds * 0.2f)
                            + 0.5f * std::cos(Ry * 3.1f + Rx * 1.7f);
        return Network - std::max(0.0f, R - 3.2f) * 1.4f;
    };

    for (auto& Slot : Slots)
    {
        if (!IsParticleSlotAlive(Slot)) continue;

        const float Heading = Slot.SpinRadians;
        const float ProbeC  = SampleScent(Slot.PositionMeters[0] + std::cos(Heading) * SenseDist, Slot.PositionMeters[1], Slot.PositionMeters[2] + std::sin(Heading) * SenseDist);
        const float ProbeL  = SampleScent(Slot.PositionMeters[0] + std::cos(Heading + Spec.SensorAngleRad) * SenseDist, Slot.PositionMeters[1], Slot.PositionMeters[2] + std::sin(Heading + Spec.SensorAngleRad) * SenseDist);
        const float ProbeR  = SampleScent(Slot.PositionMeters[0] + std::cos(Heading - Spec.SensorAngleRad) * SenseDist, Slot.PositionMeters[1], Slot.PositionMeters[2] + std::sin(Heading - Spec.SensorAngleRad) * SenseDist);

        float NextHeading = Heading;
        if (ProbeL > ProbeC && ProbeL > ProbeR) NextHeading += TurnStep;
        else if (ProbeR > ProbeC && ProbeR > ProbeL) NextHeading -= TurnStep;
        else NextHeading += (Slot.SeedScalar - 0.5f) * TurnStep * 0.35f;

        Slot.SpinRadians    = NextHeading;
        Slot.VelocityMps[0] = std::cos(NextHeading) * Speed;
        Slot.VelocityMps[1] = std::sin(ElapsedSeconds * 0.9f + Slot.SeedScalar * 6.28f) * 0.25f;
        Slot.VelocityMps[2] = std::sin(NextHeading) * Speed;
        Slot.AuxScalar      = std::clamp(0.5f + 0.5f * ProbeC, 0.0f, 1.0f);
    }
}

} // namespace Frontier::ParticleSpace
