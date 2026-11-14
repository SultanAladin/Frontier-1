/*============================================================================================================================================
                                                     PARTICLEMETRICSCOLLECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleMetricsCollector for CPU & Vulkan GPU reduction telemetry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"

#include "Engine/ParticleSpace/Dynamics/MolecularReactionDynamics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::ParticleSpace
{

ParticleSimulationMetrics ParticleMetricsCollector::CollectFromSlots(
    std::span<const ParticleSlot> Slots,
    float                         LastStepMillis,
    bool                          ExecutedOnVulkanGpu) noexcept
{
    ParticleSimulationMetrics Out {};
    Out.TotalCapacity       = static_cast<uint32_t>(Slots.size());
    Out.LastStepMillis      = LastStepMillis;
    Out.ExecutedOnVulkanGpu = ExecutedOnVulkanGpu;

    float MinB[3] = { std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    float MaxB[3] = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    double SumSpeed   = 0.0;
    double SumKinetic = 0.0;
    float  MaxSpeed   = 0.0f;

    for (const auto& S : Slots)
    {
        if (!IsParticleSlotAlive(S)) continue;

        const bool Finite = std::isfinite(S.PositionMeters[0]) && std::isfinite(S.PositionMeters[1]) && std::isfinite(S.PositionMeters[2])
                         && std::isfinite(S.VelocityMps[0])    && std::isfinite(S.VelocityMps[1])    && std::isfinite(S.VelocityMps[2]);
        if (!Finite)
        {
            ++Out.NaNOrInfCount;
            continue;
        }

        ++Out.LiveCount;
        const float SpdSq = S.VelocityMps[0] * S.VelocityMps[0]
                          + S.VelocityMps[1] * S.VelocityMps[1]
                          + S.VelocityMps[2] * S.VelocityMps[2];
        const float Spd = std::sqrt(SpdSq);
        SumSpeed   += static_cast<double>(Spd);
        SumKinetic += 0.5 * static_cast<double>(SpdSq);
        MaxSpeed    = std::max(MaxSpeed, Spd);

        for (int K = 0; K < 3; ++K)
        {
            MinB[K] = std::min(MinB[K], S.PositionMeters[K]);
            MaxB[K] = std::max(MaxB[K], S.PositionMeters[K]);
        }

        const uint32_t Sp = QueryMolecularSpecies(S);
        if (Sp == MolecularSpeciesA)      ++Out.SpeciesACount;
        else if (Sp == MolecularSpeciesB) ++Out.SpeciesBCount;
        else                              ++Out.SpeciesCCount;
    }

    if (Out.LiveCount > 0u)
    {
        Out.MeanSpeedMps       = static_cast<float>(SumSpeed / static_cast<double>(Out.LiveCount));
        Out.MaxSpeedMps        = MaxSpeed;
        Out.TotalKineticJoules = static_cast<float>(SumKinetic);
        for (int K = 0; K < 3; ++K)
        {
            Out.BoundsMinMeters[K] = MinB[K];
            Out.BoundsMaxMeters[K] = MaxB[K];
        }
    }
    return Out;
}

ParticleSimulationMetrics ParticleMetricsCollector::DecodeFromGpuReduction(
    const ParticleMetricsReductionGpuBuffer& GpuBuf,
    uint32_t                                 TotalCapacity,
    float                                    LastStepMillis) noexcept
{
    ParticleSimulationMetrics Out {};
    Out.TotalCapacity       = TotalCapacity;
    Out.LiveCount           = GpuBuf.LiveCount;
    Out.LastStepMillis      = LastStepMillis;
    Out.ExecutedOnVulkanGpu = true;
    Out.SpeciesACount       = GpuBuf.SpeciesACount;
    Out.SpeciesBCount       = GpuBuf.SpeciesBCount;
    Out.SpeciesCCount       = GpuBuf.SpeciesCCount;

    if (GpuBuf.LiveCount > 0u)
    {
        Out.MeanSpeedMps       = (static_cast<float>(GpuBuf.SumSpeedMilli) * 0.001f) / static_cast<float>(GpuBuf.LiveCount);
        Out.MaxSpeedMps        = static_cast<float>(GpuBuf.MaxSpeedMilli) * 0.001f;
        Out.TotalKineticJoules = static_cast<float>(GpuBuf.SumKineticMilli) * 0.001f;
        Out.BoundsMinMeters[0] = static_cast<float>(GpuBuf.MinXmm) * 0.001f;
        Out.BoundsMinMeters[1] = static_cast<float>(GpuBuf.MinYmm) * 0.001f;
        Out.BoundsMinMeters[2] = static_cast<float>(GpuBuf.MinZmm) * 0.001f;
        Out.BoundsMaxMeters[0] = static_cast<float>(GpuBuf.MaxXmm) * 0.001f;
        Out.BoundsMaxMeters[1] = static_cast<float>(GpuBuf.MaxYmm) * 0.001f;
        Out.BoundsMaxMeters[2] = static_cast<float>(GpuBuf.MaxZmm) * 0.001f;
    }
    return Out;
}

} // namespace Frontier::ParticleSpace
