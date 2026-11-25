/*============================================================================================================================================
                                               SIMULATIONBUDGETGOVERNOR.CPP
============================================================================================================================================*/
// 🧩 Phase H03 Implementation of SimulationBudgetGovernor (zero-allocation EWMA hysteresis tier scaling)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationBudgetGovernor.h"

#include <algorithm>

namespace Frontier::SimulationSpace
{

SimulationBudgetGovernor::SimulationBudgetGovernor(const SimulationBudgetPolicy& policy) noexcept
    : Policy_(policy)
{
    Policy_.TargetSimBudgetMs       = std::max(0.25f, Policy_.TargetSimBudgetMs);
    Policy_.EwmaAlpha               = std::clamp(Policy_.EwmaAlpha, 0.05f, 1.0f);
    Policy_.DowngradeHysteresisHits = std::max(1u, Policy_.DowngradeHysteresisHits);
    Policy_.UpgradeHysteresisHits   = std::max(1u, Policy_.UpgradeHysteresisHits);
    RefreshTierRecommendations();
}

void SimulationBudgetGovernor::Reset() noexcept
{
    Telemetry_         = {};
    OverBudgetStreak_  = 0u;
    UnderBudgetStreak_ = 0u;
    RefreshTierRecommendations();
}

void SimulationBudgetGovernor::RefreshTierRecommendations() noexcept
{
    switch (Telemetry_.ActiveTier)
    {
        case SimulationQualityTier::Ultra:
            Telemetry_.RecommendedPressureIters  = 24u;
            Telemetry_.RecommendedFullRateRadius = 30.0f;
            Telemetry_.RecommendedHalfRateRadius = 65.0f;
            Telemetry_.RecommendedSleepRadius    = 95.0f;
            break;
        case SimulationQualityTier::High:
            Telemetry_.RecommendedPressureIters  = 18u;
            Telemetry_.RecommendedFullRateRadius = 24.0f;
            Telemetry_.RecommendedHalfRateRadius = 52.0f;
            Telemetry_.RecommendedSleepRadius    = 78.0f;
            break;
        case SimulationQualityTier::Balanced:
            Telemetry_.RecommendedPressureIters  = 12u;
            Telemetry_.RecommendedFullRateRadius = 18.0f;
            Telemetry_.RecommendedHalfRateRadius = 40.0f;
            Telemetry_.RecommendedSleepRadius    = 62.0f;
            break;
        case SimulationQualityTier::Performance:
            Telemetry_.RecommendedPressureIters  = 8u;
            Telemetry_.RecommendedFullRateRadius = 12.0f;
            Telemetry_.RecommendedHalfRateRadius = 28.0f;
            Telemetry_.RecommendedSleepRadius    = 45.0f;
            break;
    }
}

void SimulationBudgetGovernor::RecordStepDurationMs(float measuredMs) noexcept
{
    const float clamped = std::max(0.0f, measuredMs);
    if (Telemetry_.SamplesRecorded == 0ull)
    {
        Telemetry_.SmoothedStepMs = clamped;
    }
    else
    {
        Telemetry_.SmoothedStepMs =
            Policy_.EwmaAlpha * clamped + (1.0f - Policy_.EwmaAlpha) * Telemetry_.SmoothedStepMs;
    }
    Telemetry_.PeakStepMs       = std::max(Telemetry_.PeakStepMs, clamped);
    Telemetry_.SamplesRecorded += 1ull;

    const float downgradeLimit = Policy_.TargetSimBudgetMs * 1.05f;
    const float upgradeLimit   = Policy_.TargetSimBudgetMs * 0.65f;

    if (Telemetry_.SmoothedStepMs > downgradeLimit)
    {
        ++OverBudgetStreak_;
        UnderBudgetStreak_ = 0u;
        if (OverBudgetStreak_ >= Policy_.DowngradeHysteresisHits &&
            Telemetry_.ActiveTier != SimulationQualityTier::Performance)
        {
            Telemetry_.ActiveTier = static_cast<SimulationQualityTier>(static_cast<uint8_t>(Telemetry_.ActiveTier) + 1u);
            Telemetry_.TierTransitions += 1u;
            OverBudgetStreak_ = 0u;
            RefreshTierRecommendations();
        }
    }
    else if (Telemetry_.SmoothedStepMs < upgradeLimit)
    {
        ++UnderBudgetStreak_;
        OverBudgetStreak_ = 0u;
        if (UnderBudgetStreak_ >= Policy_.UpgradeHysteresisHits &&
            Telemetry_.ActiveTier != SimulationQualityTier::Ultra)
        {
            Telemetry_.ActiveTier = static_cast<SimulationQualityTier>(static_cast<uint8_t>(Telemetry_.ActiveTier) - 1u);
            Telemetry_.TierTransitions += 1u;
            UnderBudgetStreak_ = 0u;
            RefreshTierRecommendations();
        }
    }
    else
    {
        OverBudgetStreak_  = 0u;
        UnderBudgetStreak_ = 0u;
    }
}

void SimulationBudgetGovernor::ApplyToCoordinator(SimulationCoordinatorExtension& coordinator) const noexcept
{
    coordinator.AccessParameters().FluidSettings.PressureIterations = Telemetry_.RecommendedPressureIters;

    Deformable::DeformableWorldCullingPolicy cull {};
    cull.FullRateRadiusMeters = Telemetry_.RecommendedFullRateRadius;
    cull.HalfRateRadiusMeters = Telemetry_.RecommendedHalfRateRadius;
    cull.SleepRadiusMeters    = Telemetry_.RecommendedSleepRadius;
    coordinator.AccessDeformables().AccessWorldRegistry().SetCullingPolicy(cull);
}

} // namespace Frontier::SimulationSpace
