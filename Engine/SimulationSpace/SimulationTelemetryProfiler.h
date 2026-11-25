/*============================================================================================================================================
                                               SIMULATIONTELEMETRYPROFILER.H
============================================================================================================================================*/
// 🧩 Phase I02 Zero-Allocation Fixed-Ring Simulation Timing Profiler recording wall-clock microseconds per tick and
//    computing p50 / p95 / p99 / mean / peak percentiles against target budget (248 FPS = 2.0 ms sim budget) with 0 heap allocs.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              TIMING SAMPLE & PERCENTILES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationTickTimingSample
{
    uint64_t TickOrdinal    = 0ull;
    float    TotalMicros    = 0.0f;
    float    BudgetRatio    = 0.0f; // TotalMicros / (TargetBudgetMs * 1000)
};

struct SimulationTimingPercentiles
{
    uint32_t SampleCount       = 0u;
    float    MeanMicros        = 0.0f;
    float    P50Micros         = 0.0f;
    float    P95Micros         = 0.0f;
    float    P99Micros         = 0.0f;
    float    PeakMicros        = 0.0f;
    float    TargetBudgetMs    = 2.0f;
    bool     WithinP95Budget   = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           SIMULATION TELEMETRY PROFILER
//------------------------------------------------------------------------------------------------------------------------

class SimulationTelemetryProfiler
{
public:
    static constexpr uint32_t kWindowCapacity = 128u;

    explicit SimulationTelemetryProfiler(float targetBudgetMs = 2.0f) noexcept;

    /// Records one measured simulation tick duration in microseconds (0 heap allocations, noexcept).
    void RecordTickMicros(uint64_t tickOrdinal, float durationMicros) noexcept;

    /// Computes exact p50 / p95 / p99 / mean / peak over the active window using stack scratch only (0 heap allocs).
    [[nodiscard]] SimulationTimingPercentiles ComputePercentiles() const noexcept;

    /// Formats a clean Markdown telemetry table (off-hot-path diagnostic export).
    [[nodiscard]] std::string FormatMarkdownSummary() const;

    void SetTargetBudgetMs(float targetBudgetMs) noexcept;
    void Reset() noexcept;

    [[nodiscard]] uint32_t QuerySampleCount() const noexcept { return SampleCount_; }

private:
    float                                                     TargetBudgetMs_ = 2.0f;
    std::array<SimulationTickTimingSample, kWindowCapacity>   RingSamples_    {};
    uint32_t                                                  NextWriteSlot_  = 0u;
    uint32_t                                                  SampleCount_    = 0u;
    float                                                     AllTimePeakUs_  = 0.0f;
};

} // namespace Frontier::SimulationSpace
