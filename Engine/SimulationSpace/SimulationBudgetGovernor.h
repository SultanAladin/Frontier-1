/*============================================================================================================================================
                                                SIMULATIONBUDGETGOVERNOR.H
============================================================================================================================================*/
// 🧩 Phase H03 Zero-Allocation Real-Time Simulation Budget Governor protecting 248 FPS / 120 FPS / 60 FPS budgets via
//    hysteresis-stabilized adaptive quality tiers (Fluid pressure iterations + Deformable LOD culling radii).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <cstdint>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             ADAPTIVE QUALITY TIERS
//------------------------------------------------------------------------------------------------------------------------

enum class SimulationQualityTier : uint8_t
{
    Ultra       = 0u, // Full Fluid Jacobi iterations (24) & maximum Deformable culling radii (30 / 65 / 95 m)
    High        = 1u, // 18 pressure iterations & 24 / 52 / 78 m culling radii
    Balanced    = 2u, // 12 pressure iterations & 18 / 40 / 62 m culling radii
    Performance = 3u  // 8 pressure iterations & tight 12 / 28 / 45 m culling radii
};

struct SimulationBudgetPolicy
{
    float    TargetSimBudgetMs       = 2.0f;  // [ms] - Target per-step simulation budget (2.0 ms for 248 FPS target)
    float    EwmaAlpha               = 0.25f; // [-]  - Exponential smoothing factor for measured step duration
    uint32_t DowngradeHysteresisHits = 4u;    // [-]  - Consecutive over-budget samples before stepping down a tier
    uint32_t UpgradeHysteresisHits   = 12u;   // [-]  - Consecutive under-budget (< 65%) samples before stepping up
};

struct SimulationBudgetTelemetry
{
    SimulationQualityTier ActiveTier                = SimulationQualityTier::Ultra;
    float                 SmoothedStepMs            = 0.0f;
    float                 PeakStepMs                = 0.0f;
    uint32_t              RecommendedPressureIters  = 24u;
    float                 RecommendedFullRateRadius = 30.0f;
    float                 RecommendedHalfRateRadius = 65.0f;
    float                 RecommendedSleepRadius    = 95.0f;
    uint64_t              SamplesRecorded           = 0ull;
    uint32_t              TierTransitions           = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             SIMULATION BUDGET GOVERNOR
//------------------------------------------------------------------------------------------------------------------------

class SimulationBudgetGovernor
{
public:
    explicit SimulationBudgetGovernor(const SimulationBudgetPolicy& policy = {}) noexcept;

    /// Records one measured simulation tick duration (ms) and updates the adaptive quality tier (0 heap allocations).
    void RecordStepDurationMs(float measuredMs) noexcept;

    /// Applies the active quality tier's Fluid pressure iteration budget and Deformable culling radii to `coordinator`.
    void ApplyToCoordinator(SimulationCoordinatorExtension& coordinator) const noexcept;

    void Reset() noexcept;

    [[nodiscard]] const SimulationBudgetPolicy&    InspectPolicy() const noexcept { return Policy_; }
    [[nodiscard]] const SimulationBudgetTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }

private:
    void RefreshTierRecommendations() noexcept;

    SimulationBudgetPolicy    Policy_             {};
    SimulationBudgetTelemetry Telemetry_          {};
    uint32_t                  OverBudgetStreak_   = 0u;
    uint32_t                  UnderBudgetStreak_  = 0u;
};

} // namespace Frontier::SimulationSpace
