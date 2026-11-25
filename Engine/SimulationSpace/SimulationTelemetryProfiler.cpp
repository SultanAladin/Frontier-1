/*============================================================================================================================================
                                              SIMULATIONTELEMETRYPROFILER.CPP
============================================================================================================================================*/
// 🧩 Phase I02 Implementation of SimulationTelemetryProfiler (zero-allocation stack-sorted percentiles)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationTelemetryProfiler.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace Frontier::SimulationSpace
{

SimulationTelemetryProfiler::SimulationTelemetryProfiler(float targetBudgetMs) noexcept
{
    SetTargetBudgetMs(targetBudgetMs);
}

void SimulationTelemetryProfiler::SetTargetBudgetMs(float targetBudgetMs) noexcept
{
    TargetBudgetMs_ = std::max(0.1f, targetBudgetMs);
}

void SimulationTelemetryProfiler::Reset() noexcept
{
    NextWriteSlot_ = 0u;
    SampleCount_   = 0u;
    AllTimePeakUs_ = 0.0f;
}

void SimulationTelemetryProfiler::RecordTickMicros(uint64_t tickOrdinal, float durationMicros) noexcept
{
    const float clampedUs = std::max(0.0f, durationMicros);
    const float budgetUs  = TargetBudgetMs_ * 1000.0f;

    SimulationTickTimingSample& dst = RingSamples_[NextWriteSlot_];
    dst.TickOrdinal = tickOrdinal;
    dst.TotalMicros = clampedUs;
    dst.BudgetRatio = (budgetUs > 0.0f) ? (clampedUs / budgetUs) : 0.0f;

    NextWriteSlot_ = (NextWriteSlot_ + 1u) % kWindowCapacity;
    if (SampleCount_ < kWindowCapacity)
    {
        ++SampleCount_;
    }
    AllTimePeakUs_ = std::max(AllTimePeakUs_, clampedUs);
}

SimulationTimingPercentiles SimulationTelemetryProfiler::ComputePercentiles() const noexcept
{
    SimulationTimingPercentiles out {};
    out.SampleCount    = SampleCount_;
    out.TargetBudgetMs = TargetBudgetMs_;
    out.PeakMicros     = AllTimePeakUs_;

    if (SampleCount_ == 0u)
    {
        out.WithinP95Budget = true;
        return out;
    }

    std::array<float, kWindowCapacity> sorted {};
    double sumUs = 0.0;
    for (uint32_t i = 0u; i < SampleCount_; ++i)
    {
        const float v = RingSamples_[i].TotalMicros;
        sorted[i] = v;
        sumUs    += static_cast<double>(v);
    }
    std::sort(sorted.begin(), sorted.begin() + SampleCount_);

    auto pickPercentile = [&](float pct) noexcept -> float
    {
        const float rawIdx = pct * static_cast<float>(SampleCount_ - 1u);
        const uint32_t idx = std::min(SampleCount_ - 1u, static_cast<uint32_t>(rawIdx + 0.5f));
        return sorted[idx];
    };

    out.MeanMicros      = static_cast<float>(sumUs / static_cast<double>(SampleCount_));
    out.P50Micros       = pickPercentile(0.50f);
    out.P95Micros       = pickPercentile(0.95f);
    out.P99Micros       = pickPercentile(0.99f);
    out.WithinP95Budget = (out.P95Micros <= TargetBudgetMs_ * 1000.0f);
    return out;
}

std::string SimulationTelemetryProfiler::FormatMarkdownSummary() const
{
    const SimulationTimingPercentiles p = ComputePercentiles();
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "| Metric | Value |\n";
    os << "| --- | --- |\n";
    os << "| Samples | " << p.SampleCount << " |\n";
    os << "| Mean | " << (p.MeanMicros / 1000.0f) << " ms |\n";
    os << "| P50 | " << (p.P50Micros / 1000.0f) << " ms |\n";
    os << "| P95 | " << (p.P95Micros / 1000.0f) << " ms |\n";
    os << "| P99 | " << (p.P99Micros / 1000.0f) << " ms |\n";
    os << "| Peak | " << (p.PeakMicros / 1000.0f) << " ms |\n";
    os << "| Budget (" << p.TargetBudgetMs << " ms) | " << (p.WithinP95Budget ? "PASS" : "OVER") << " |\n";
    return os.str();
}

} // namespace Frontier::SimulationSpace
