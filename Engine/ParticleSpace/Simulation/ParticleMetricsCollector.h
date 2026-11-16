/*============================================================================================================================================
                                                          PARTICLEMETRICSCOLLECTOR.H
============================================================================================================================================*/
// 🧩 Real-time telemetry & physics health collector for ParticleSpace rigs (live count, speed, kinetic energy, AABB & species counters)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Matches ParticleReductionCompute.slang ParticleMetricsReductionGpu layout exactly (64 bytes).
struct alignas(16) ParticleMetricsReductionGpuBuffer
{
    uint32_t LiveCount       = 0u;
    uint32_t SumSpeedMilli   = 0u;
    uint32_t MaxSpeedMilli   = 0u;
    uint32_t SumKineticMilli = 0u;
    int32_t  MinXmm          =  1000000;
    int32_t  MinYmm          =  1000000;
    int32_t  MinZmm          =  1000000;
    int32_t  MaxXmm          = -1000000;
    int32_t  MaxYmm          = -1000000;
    int32_t  MaxZmm          = -1000000;
    uint32_t SpeciesACount   = 0u;
    uint32_t SpeciesBCount   = 0u;
    uint32_t SpeciesCCount   = 0u;
    uint32_t Reserved0       = 0u;
    uint32_t Reserved1       = 0u;
    uint32_t Reserved2       = 0u;
};

static_assert(sizeof(ParticleMetricsReductionGpuBuffer) == 64u, "ParticleMetricsReductionGpuBuffer must remain 64 bytes.");

struct ParticleSimulationMetrics
{
    uint32_t LiveCount            = 0u;
    uint32_t TotalCapacity        = 0u;
    uint32_t NaNOrInfCount        = 0u;
    float    MeanSpeedMps         = 0.0f;
    float    MaxSpeedMps          = 0.0f;
    float    TotalKineticJoules   = 0.0f;
    float    BoundsMinMeters[3]   = { 0.0f, 0.0f, 0.0f };
    float    BoundsMaxMeters[3]   = { 0.0f, 0.0f, 0.0f };
    uint32_t SpeciesACount        = 0u;
    uint32_t SpeciesBCount        = 0u;
    uint32_t SpeciesCCount        = 0u;
    float    LastStepMillis       = 0.0f;
    bool     ExecutedOnVulkanGpu  = false;
};

class ParticleMetricsCollector
{
public:
    /// Computes complete simulation telemetry from a CPU span of ParticleSlots.
    [[nodiscard]] static ParticleSimulationMetrics CollectFromSlots(
        std::span<const ParticleSlot> Slots,
        float                         LastStepMillis      = 0.0f,
        bool                          ExecutedOnVulkanGpu = false) noexcept;

    /// Converts a GPU-reduced ParticleMetricsReductionGpuBuffer readback into ParticleSimulationMetrics.
    [[nodiscard]] static ParticleSimulationMetrics DecodeFromGpuReduction(
        const ParticleMetricsReductionGpuBuffer& GpuBuf,
        uint32_t                                 TotalCapacity,
        float                                    LastStepMillis = 0.0f) noexcept;
};

} // namespace Frontier::ParticleSpace
