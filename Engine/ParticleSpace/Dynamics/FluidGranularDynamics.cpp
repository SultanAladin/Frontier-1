/*============================================================================================================================================
                                                          FLUIDGRANULARDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Implementation of SPH Fluid, DEM Granular collisions, Elastic PBD Strands, Thermal/Fireworks, Dielectric Arcs, Field Matrix & Flora

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/FluidGranularDynamics.h"

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

void StepFluidSphDynamics(
    const FluidSphSpec&     Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const float H       = std::max(0.18f, Spec.SmoothingRadiusM);
    const float HSq     = H * H;
    const float InvCell = 1.0f / H;

    std::unordered_map<int64_t, std::vector<uint32_t>> Grid;
    Grid.reserve(Slots.size());
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        if (!QueryParticleSlotAlive(Slots[I])) continue;
        const int64_t Key = HashCell3D(
            CellCoord(Slots[I].PositionMeters[0], InvCell),
            CellCoord(Slots[I].PositionMeters[1], InvCell),
            CellCoord(Slots[I].PositionMeters[2], InvCell));
        Grid[Key].push_back(I);
    }

    // Pass 1: Poly6 density & Tait-style linear pressure
    std::vector<float> Density(Slots.size(), 1.0f);
    std::vector<float> Pressure(Slots.size(), 0.0f);

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        if (!QueryParticleSlotAlive(Slots[I])) continue;
        const int32_t Cx = CellCoord(Slots[I].PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(Slots[I].PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(Slots[I].PositionMeters[2], InvCell);

        float Rho = 0.0f;
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
                        const float Rx = Slots[I].PositionMeters[0] - Slots[J].PositionMeters[0];
                        const float Ry = Slots[I].PositionMeters[1] - Slots[J].PositionMeters[1];
                        const float Rz = Slots[I].PositionMeters[2] - Slots[J].PositionMeters[2];
                        const float D2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (D2 >= HSq) continue;
                        const float Diff = (HSq - D2) / HSq;
                        Rho += Diff * Diff * Diff;
                    }
                }
            }
        }
        Density[I]  = std::max(0.35f, Rho);
        Pressure[I] = Spec.PressureStiffness * std::max(0.0f, Density[I] - Spec.RestDensity);
        Slots[I].AuxScalar = std::clamp(Density[I] / (Spec.RestDensity * 1.8f), 0.0f, 1.0f);
    }

    // Pass 2: Spiky pressure gradient + Laplacian viscosity + tank containment
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& Self = Slots[I];
        if (!QueryParticleSlotAlive(Self)) continue;

        const int32_t Cx = CellCoord(Self.PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(Self.PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(Self.PositionMeters[2], InvCell);

        float Ax = 0.0f, Ay = 0.0f, Az = 0.0f;
        uint32_t Neighbors = 0u;
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
                        if (J == I || Neighbors >= 24u) continue;
                        const float Rx = Self.PositionMeters[0] - Slots[J].PositionMeters[0];
                        const float Ry = Self.PositionMeters[1] - Slots[J].PositionMeters[1];
                        const float Rz = Self.PositionMeters[2] - Slots[J].PositionMeters[2];
                        const float D2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (D2 < 1e-6f || D2 >= HSq) continue;

                        const float D     = std::sqrt(D2);
                        const float Spiky = (H - D) / H;
                        const float PAvg  = 0.5f * (Pressure[I] + Pressure[J]) / std::max(0.35f, Density[J]);
                        const float PPush = PAvg * Spiky * Spiky / D;
                        Ax += Rx * PPush;
                        Ay += Ry * PPush;
                        Az += Rz * PPush;

                        const float Visc = Spec.ViscosityCoeff * Spiky / std::max(0.35f, Density[J]);
                        Ax += (Slots[J].VelocityMps[0] - Self.VelocityMps[0]) * Visc;
                        Ay += (Slots[J].VelocityMps[1] - Self.VelocityMps[1]) * Visc;
                        Az += (Slots[J].VelocityMps[2] - Self.VelocityMps[2]) * Visc;
                        ++Neighbors;
                    }
                }
            }
        }

        Self.VelocityMps[0] += std::clamp(Ax, -40.0f, 40.0f) * DeltaSeconds;
        Self.VelocityMps[1] += std::clamp(Ay, -40.0f, 40.0f) * DeltaSeconds;
        Self.VelocityMps[2] += std::clamp(Az, -40.0f, 40.0f) * DeltaSeconds;

        // Tank bounding walls
        const float HalfW = std::max(0.6f, Spec.TankHalfWidthM);
        const float MinX = OriginCenter[0] - HalfW, MaxX = OriginCenter[0] + HalfW;
        const float MinZ = OriginCenter[2] - HalfW, MaxZ = OriginCenter[2] + HalfW;
        if (Self.PositionMeters[0] < MinX) { Self.PositionMeters[0] = MinX; if (Self.VelocityMps[0] < 0.0f) Self.VelocityMps[0] *= -0.35f; }
        if (Self.PositionMeters[0] > MaxX) { Self.PositionMeters[0] = MaxX; if (Self.VelocityMps[0] > 0.0f) Self.VelocityMps[0] *= -0.35f; }
        if (Self.PositionMeters[2] < MinZ) { Self.PositionMeters[2] = MinZ; if (Self.VelocityMps[2] < 0.0f) Self.VelocityMps[2] *= -0.35f; }
        if (Self.PositionMeters[2] > MaxZ) { Self.PositionMeters[2] = MaxZ; if (Self.VelocityMps[2] > 0.0f) Self.VelocityMps[2] *= -0.35f; }
    }
}

void StepGranularCollisionDynamics(
    const GranularCollisionSpec& Spec,
    const float                  OriginCenter[3],
    std::span<ParticleSlot>      Slots,
    float                        DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;

    const float GrainR  = std::max(0.03f, Spec.GrainRadiusMeters);
    const float Diam    = GrainR * 2.0f;
    const float DiamSq  = Diam * Diam;
    const float InvCell = 1.0f / Diam;

    std::unordered_map<int64_t, std::vector<uint32_t>> Grid;
    Grid.reserve(Slots.size());
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        if (!QueryParticleSlotAlive(Slots[I])) continue;
        const int64_t Key = HashCell3D(
            CellCoord(Slots[I].PositionMeters[0], InvCell),
            CellCoord(Slots[I].PositionMeters[1], InvCell),
            CellCoord(Slots[I].PositionMeters[2], InvCell));
        Grid[Key].push_back(I);
    }

    for (uint32_t I = 0u; I < static_cast<uint32_t>(Slots.size()); ++I)
    {
        ParticleSlot& A = Slots[I];
        if (!QueryParticleSlotAlive(A)) continue;

        const int32_t Cx = CellCoord(A.PositionMeters[0], InvCell);
        const int32_t Cy = CellCoord(A.PositionMeters[1], InvCell);
        const int32_t Cz = CellCoord(A.PositionMeters[2], InvCell);

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
                        const float D2 = Rx * Rx + Ry * Ry + Rz * Rz;
                        if (D2 <= 1e-8f || D2 >= DiamSq) continue;

                        const float D  = std::sqrt(D2);
                        const float Nx = Rx / D, Ny = Ry / D, Nz = Rz / D;
                        const float Overlap = 0.5f * (Diam - D);

                        // Positional depenetration
                        A.PositionMeters[0] -= Nx * Overlap;
                        A.PositionMeters[1] -= Ny * Overlap;
                        A.PositionMeters[2] -= Nz * Overlap;
                        B.PositionMeters[0] += Nx * Overlap;
                        B.PositionMeters[1] += Ny * Overlap;
                        B.PositionMeters[2] += Nz * Overlap;

                        // Normal impulse + Coulomb tangential friction
                        const float Rvx = B.VelocityMps[0] - A.VelocityMps[0];
                        const float Rvy = B.VelocityMps[1] - A.VelocityMps[1];
                        const float Rvz = B.VelocityMps[2] - A.VelocityMps[2];
                        const float Vn  = Rvx * Nx + Rvy * Ny + Rvz * Nz;
                        if (Vn < 0.0f)
                        {
                            const float Imp = -0.5f * (1.0f + Spec.Restitution) * Vn;
                            A.VelocityMps[0] -= Nx * Imp; A.VelocityMps[1] -= Ny * Imp; A.VelocityMps[2] -= Nz * Imp;
                            B.VelocityMps[0] += Nx * Imp; B.VelocityMps[1] += Ny * Imp; B.VelocityMps[2] += Nz * Imp;

                            const float Tx = Rvx - Vn * Nx, Ty = Rvy - Vn * Ny, Tz = Rvz - Vn * Nz;
                            const float Fric = std::clamp(Spec.FrictionCoeff * 0.25f, 0.0f, 0.45f);
                            A.VelocityMps[0] += Tx * Fric; A.VelocityMps[1] += Ty * Fric; A.VelocityMps[2] += Tz * Fric;
                            B.VelocityMps[0] -= Tx * Fric; B.VelocityMps[1] -= Ty * Fric; B.VelocityMps[2] -= Tz * Fric;
                        }
                    }
                }
            }
        }

        // Keep grains inside a soft arena basin around OriginCenter
        const float Bnd = 3.2f;
        if (A.PositionMeters[0] < OriginCenter[0] - Bnd) { A.PositionMeters[0] = OriginCenter[0] - Bnd; A.VelocityMps[0] = std::fabs(A.VelocityMps[0]) * Spec.Restitution; }
        if (A.PositionMeters[0] > OriginCenter[0] + Bnd) { A.PositionMeters[0] = OriginCenter[0] + Bnd; A.VelocityMps[0] = -std::fabs(A.VelocityMps[0]) * Spec.Restitution; }
        if (A.PositionMeters[2] < OriginCenter[2] - Bnd) { A.PositionMeters[2] = OriginCenter[2] - Bnd; A.VelocityMps[2] = std::fabs(A.VelocityMps[2]) * Spec.Restitution; }
        if (A.PositionMeters[2] > OriginCenter[2] + Bnd) { A.PositionMeters[2] = OriginCenter[2] + Bnd; A.VelocityMps[2] = -std::fabs(A.VelocityMps[2]) * Spec.Restitution; }
    }
}

void StepElasticStrandDynamics(
    const ElasticStrandSpec& Spec,
    std::span<ParticleSlot>  Slots,
    float                    DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.size() < 2u) return;

    const float Stiff   = std::clamp(Spec.StretchStiffness, 0.05f, 1.0f);
    const float RestLen = std::max(0.04f, Spec.RestSegmentMeters);
    const uint32_t Iter = std::clamp<uint32_t>(Spec.SolverIterations, 1u, 8u);

    // Gentle structural weft restoration so flags, sails, curtains & drapes billow gracefully instead of collapsing to 1D
    const float SoftRestore = std::clamp(2.4f * Stiff * DeltaSeconds, 0.0f, 0.35f);
    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& S = Slots[I];
        if (!QueryParticleSlotAlive(S)) continue;
        const float Wave = std::sin(S.AgeSeconds * 3.4f + S.OriginAnchor[0] * 1.9f + S.OriginAnchor[1] * 1.1f) * 0.24f;
        const float Gust = std::cos(S.AgeSeconds * 2.1f + S.OriginAnchor[0] * 1.3f) * 0.14f;
        S.PositionMeters[0] += ((S.OriginAnchor[0] + Gust) - S.PositionMeters[0]) * SoftRestore;
        S.PositionMeters[1] += (S.OriginAnchor[1] - S.PositionMeters[1]) * (SoftRestore * 0.45f);
        S.PositionMeters[2] += ((S.OriginAnchor[2] + Wave) - S.PositionMeters[2]) * SoftRestore;
        S.AuxScalar = std::clamp((S.OriginAnchor[0] + 2.5f) * 0.22f, 0.0f, 1.0f);
    }

    for (uint32_t It = 0u; It < Iter; ++It)
    {
        for (std::size_t I = 0u; I + 1u < Slots.size(); ++I)
        {
            ParticleSlot& A = Slots[I];
            ParticleSlot& B = Slots[I + 1u];
            if (!QueryParticleSlotAlive(A) || !QueryParticleSlotAlive(B)) continue;

            // Pin every 16th strand root node to its rest OriginAnchor
            const bool PinA = (I % 16u == 0u);
            if (PinA)
            {
                for (int K = 0; K < 3; ++K)
                {
                    A.PositionMeters[K] = A.OriginAnchor[K];
                    A.VelocityMps[K]    = 0.0f;
                }
            }
            if ((I + 1u) % 16u == 0u) continue; // Separate strand break

            const float Dx = B.PositionMeters[0] - A.PositionMeters[0];
            const float Dy = B.PositionMeters[1] - A.PositionMeters[1];
            const float Dz = B.PositionMeters[2] - A.PositionMeters[2];
            const float D  = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) + 1e-6f;
            const float Err = (D - RestLen) / D;
            const float Corr = Err * Stiff;

            if (PinA)
            {
                B.PositionMeters[0] -= Dx * Corr;
                B.PositionMeters[1] -= Dy * Corr;
                B.PositionMeters[2] -= Dz * Corr;
                B.VelocityMps[0]    -= (Dx * Corr) / std::max(0.008f, DeltaSeconds) * 0.2f;
                B.VelocityMps[1]    -= (Dy * Corr) / std::max(0.008f, DeltaSeconds) * 0.2f;
                B.VelocityMps[2]    -= (Dz * Corr) / std::max(0.008f, DeltaSeconds) * 0.2f;
            }
            else
            {
                const float Half = 0.5f * Corr;
                A.PositionMeters[0] += Dx * Half; A.PositionMeters[1] += Dy * Half; A.PositionMeters[2] += Dz * Half;
                B.PositionMeters[0] -= Dx * Half; B.PositionMeters[1] -= Dy * Half; B.PositionMeters[2] -= Dz * Half;
            }
        }
    }
}

void StepThermalFireworkDynamics(
    const ThermalFireworkSpec& Spec,
    std::span<ParticleSlot>    Slots,
    float                      ElapsedSeconds,
    float                      DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        const float NormAge = (Slot.LifespanSeconds > 1e-5f)
            ? std::clamp(Slot.AgeSeconds / Slot.LifespanSeconds, 0.0f, 1.0f)
            : 0.0f;
        const float ThermalFrac = std::exp(-Spec.CoolingRatePerSec * NormAge * 2.5f);
        Slot.AuxScalar = ThermalFrac;

        // Hot combustion thermal updraft that cools with age
        Slot.VelocityMps[1] += Spec.ThermalRiseMps2 * ThermalFrac * DeltaSeconds;

        // Crackle shimmer bursts near tail of firework star lifetime
        if (Spec.EnableCracklePop && NormAge > 0.72f)
        {
            const float Flicker = std::sin(ElapsedSeconds * 42.0f + Slot.SeedScalar * 97.0f);
            if (Flicker > 0.85f)
            {
                Slot.VelocityMps[0] += std::cos(Slot.SeedScalar * 123.4f) * 3.5f * DeltaSeconds;
                Slot.VelocityMps[1] += std::sin(Slot.SeedScalar * 77.1f)  * 3.5f * DeltaSeconds;
                Slot.VelocityMps[2] += std::cos(Slot.SeedScalar * 51.9f)  * 3.5f * DeltaSeconds;
            }
        }
    }
}

void StepDielectricArcDynamics(
    const DielectricArcSpec& Spec,
    const float              OriginCenter[3],
    std::span<ParticleSlot>  Slots,
    float                    ElapsedSeconds,
    float                    DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float QuantTime = std::floor(ElapsedSeconds * std::max(1.0f, Spec.RefreshRateHz));
    const bool  IsBeam    = (Spec.JitterAmplitudeM < 0.08f);
    constexpr uint32_t BranchCount = 5u;

    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& Slot = Slots[I];
        if (!QueryParticleSlotAlive(Slot)) continue;
        // Multi-strand midpoint-displaced plasma filaments between cathode & anode electrodes
        const uint32_t Branch = IsBeam ? 0u : static_cast<uint32_t>(I % BranchCount);
        const float SpanT  = static_cast<float>((I / (IsBeam ? 1u : BranchCount)) % 48u) / 47.0f;
        const float Env    = std::sin(SpanT * 3.14159265f);
        const float Jitter = Spec.JitterAmplitudeM * Env;
        const float BAngle = static_cast<float>(Branch) * 1.256637f + QuantTime * 0.7f;
        const float Phase  = QuantTime * 13.7f + SpanT * 23.0f + static_cast<float>(Branch) * 4.3f;
        const float ForkR  = IsBeam ? 0.02f : (0.42f * Env * (0.5f + 0.5f * std::sin(SpanT * 9.0f + Branch)));

        Slot.PositionMeters[0] = OriginCenter[0] + (SpanT - 0.5f) * 4.6f + std::sin(Phase) * Jitter * 0.25f;
        Slot.PositionMeters[1] = OriginCenter[1] + 1.6f + std::sin(BAngle) * ForkR + std::sin(Phase * 1.7f) * Jitter;
        Slot.PositionMeters[2] = OriginCenter[2] + std::cos(BAngle) * ForkR + std::cos(Phase * 2.1f) * Jitter;
        Slot.VelocityMps[0] = 0.0f; Slot.VelocityMps[1] = 0.0f; Slot.VelocityMps[2] = 0.0f;
        Slot.AuxScalar = std::clamp(0.25f + 0.75f * Env, 0.0f, 1.0f);
    }
}

void StepFieldMatrixDynamics(
    const FieldTensorSpec&  Spec,
    const float             OriginCenter[3],
    std::span<ParticleSlot> Slots,
    float                   ElapsedSeconds,
    float                   DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float EffX = OriginCenter[0] + std::cos(ElapsedSeconds * 1.15f) * 2.4f;
    const float EffY = OriginCenter[1] + 0.6f + std::sin(ElapsedSeconds * 1.6f) * 0.45f;
    const float EffZ = OriginCenter[2] + std::sin(ElapsedSeconds * 0.85f) * 2.4f;
    const float R    = std::max(0.35f, Spec.EffectorRadiusM);

    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;
        const float Dx = Slot.OriginAnchor[0] - EffX;
        const float Dy = Slot.OriginAnchor[1] - EffY;
        const float Dz = Slot.OriginAnchor[2] - EffZ;
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const float Influence = std::clamp(1.0f - Dist / R, 0.0f, 1.0f);
        const float Smooth = Influence * Influence * (3.0f - 2.0f * Influence);

        const float TargetY = Slot.OriginAnchor[1] + Smooth * Spec.DisplacementAmplitudeM;
        const float Spring  = std::clamp(8.5f * DeltaSeconds, 0.0f, 1.0f);
        Slot.PositionMeters[0] += (Slot.OriginAnchor[0] - Slot.PositionMeters[0]) * Spring;
        Slot.PositionMeters[1] += (TargetY - Slot.PositionMeters[1]) * Spring;
        Slot.PositionMeters[2] += (Slot.OriginAnchor[2] - Slot.PositionMeters[2]) * Spring;
        Slot.AuxScalar = Smooth;
    }
}

void StepBotanicalSwayDynamics(
    const BotanicalSwaySpec& Spec,
    std::span<ParticleSlot>  Slots,
    float                    ElapsedSeconds,
    float                    DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    const float Freq = Spec.SwayFrequencyHz * 6.2831853f;
    for (auto& Slot : Slots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;
        const float HeightFrac = std::clamp(Slot.OriginAnchor[1] * 0.35f, 0.0f, 1.0f);
        const float Sway = std::sin(ElapsedSeconds * Freq + Slot.OriginAnchor[0] * 0.7f + Slot.OriginAnchor[2] * 0.5f)
                         * Spec.WindSensitivity * HeightFrac * HeightFrac * 0.32f;
        const float TargetX = Slot.OriginAnchor[0] + Sway;
        const float TargetZ = Slot.OriginAnchor[2] + Sway * 0.45f;
        const float K       = std::clamp(Spec.StemStiffness * 2.5f * DeltaSeconds, 0.0f, 1.0f);
        Slot.PositionMeters[0] += (TargetX - Slot.PositionMeters[0]) * K;
        Slot.PositionMeters[1] += (Slot.OriginAnchor[1] - Slot.PositionMeters[1]) * K;
        Slot.PositionMeters[2] += (TargetZ - Slot.PositionMeters[2]) * K;
        Slot.VelocityMps[0] = 0.0f; Slot.VelocityMps[1] = 0.0f; Slot.VelocityMps[2] = 0.0f;
        Slot.AuxScalar = HeightFrac;
    }
}

} // namespace Frontier::ParticleSpace
