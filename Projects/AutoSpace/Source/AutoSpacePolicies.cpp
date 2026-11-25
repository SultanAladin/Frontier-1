/*============================================================================================================================================
                                                          AUTOSPACEPOLICIES.CPP
============================================================================================================================================*/
// 🧩 Declarative policy defaults for project AutoSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/AutoSpacePolicies.h"

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 POLICY BUILDER DEFINITIONS
//------------------------------------------------------------------------------------------------------------------------

Frontier::Lifecycle::ProjectExecutionPolicy BuildAutoSpaceExecutionPolicy() noexcept
{
    using namespace Frontier::Lifecycle;

    ProjectExecutionPolicy Policy{};
    Policy.ProjectTitle           = "AutoSpace";
    Policy.FixedStepSeconds       = 1.0 / 60.0;
    Policy.MaximumStepSeconds     = 0.100;
    Policy.MaximumSubstepsPerTick = 4u;
    Policy.TargetStepBudgetMs     = 16.6667;
    Policy.WorkerChannelCount     = 2u;
    Policy.WindowWidthPixels      = 1280u;
    Policy.WindowHeightPixels     = 720u;
    Policy.PreferHeadlessWindow   = true;
    Policy.SuspendWhenMinimized   = true;
    Policy.EnableVerticalSync     = true;
    Policy.EnabledDomains         = RuntimeDomainMask::WorkerChannels |
                                    RuntimeDomainMask::AssetStreaming |
                                    RuntimeDomainMask::Networking;
    ProjectExecutionPolicyCodec::ClampToSafeBounds(Policy);
    return Policy;
}

Frontier::SimulationSpace::Policies::GameplayRulePolicy BuildAutoSpaceGameplayPolicy() noexcept
{
    Frontier::SimulationSpace::Policies::GameplayRulePolicy Policy{};
    Policy.SchemaVersion               = 1u;
    Policy.FixedSimulationHz           = 60u;
    Policy.AlignmentHashIntervalTicks  = 10u;
    Policy.SpeedLimitKmh               = 120.0f;
    Policy.RedSignalPenaltyScore       = 50.0f;
    Policy.CollisionImpulseThresholdNs = 12000.0f;
    Policy.PursuitEscalationStages     = 5u;
    Policy.MaximumSteerMagnitude       = 1.0f;
    Policy.MaximumDirectiveRateHz      = 240u;
    Policy.JoinInProgressAllowed       = true;
    Policy.HostMigrationEnabled        = true;
    Frontier::SimulationSpace::Policies::GameplayRulePolicyEngine::ClampToSafeBounds(Policy);
    return Policy;
}

Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy BuildAutoSpaceDrivingPolicy(uint64_t Seed) noexcept
{
    Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy Policy{};
    Policy.SeedInitialize(Seed);
    return Policy;
}

AutoSpacePolicyBundle BuildAutoSpacePolicyBundle(uint64_t DrivingSeed) noexcept
{
    AutoSpacePolicyBundle Bundle{};
    Bundle.ExecutionPolicy = BuildAutoSpaceExecutionPolicy();
    Bundle.GameplayPolicy  = BuildAutoSpaceGameplayPolicy();
    Bundle.DrivingPolicy   = BuildAutoSpaceDrivingPolicy(DrivingSeed);
    Bundle.VehicleSchedule = Frontier::NetworkingSpace::BuildRealtimeKinematicContract(2001u, 30.0f);

    Bundle.TransportInput.AccessTier = Frontier::NetworkingSpace::EntitlementAccessTier::StandardAccess;
    Bundle.TransportInput.AvailabilityMask =
        static_cast<uint8_t>(Frontier::NetworkingSpace::TransportLinkAvailabilityMask::EnableLoopback) |
        static_cast<uint8_t>(Frontier::NetworkingSpace::TransportLinkAvailabilityMask::EnableSovereignSocket);
    return Bundle;
}

} // namespace AutoSpace
