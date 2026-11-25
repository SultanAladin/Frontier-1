/*============================================================================================================================================
                                                         UNTITLEDLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Tight, zero-Editor reference project main loop implementation for Untitled

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/UntitledLifecycle.h"

#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Untitled
{

//------------------------------------------------------------------------------------------------------------------------
//                                                UNTITLED LIFECYCLE METHODS
//------------------------------------------------------------------------------------------------------------------------

UntitledLifecycle::~UntitledLifecycle() noexcept
{
    Finalize();
}

bool UntitledLifecycle::Initialize(
    const std::filesystem::path& ProjectRootPath,
    bool                         ForceHeadlessWindow,
    std::string&                 OutDiagnostic)
{
    Finalize();

    Policies                  = BuildUntitledPolicyBundle();
    Metrics                   = {};
    ReferenceVehicle          = {};
    ReferenceVehicle.ActorIdentifier = 1001u;

    if (!RuntimeLoop.BootFromProjectRoot(ProjectRootPath, OutDiagnostic, ForceHeadlessWindow))
    {
        return false;
    }

    // Harmonize project policy cadence with booted TOML configuration
    Policies.ExecutionPolicy          = RuntimeLoop.InspectExecutionPolicy();
    Policies.GameplayPolicy.FixedSimulationHz =
        static_cast<uint32_t>(std::lround(1.0 / std::max(0.001, Policies.ExecutionPolicy.FixedStepSeconds)));
    Frontier::SimulationSpace::Policies::GameplayRulePolicyEngine::ClampToSafeBounds(Policies.GameplayPolicy);

    const auto Reading            = RuntimeLoop.InspectReading();
    Metrics.BootLoadedFromDisk    = Reading.BootLoadedFromDisk;
    Metrics.WorkerChannelsRunning = Reading.CadenceMetrics.WorkerChannelsActive;
    Metrics.AssetStreamRunning    = Reading.CadenceMetrics.AssetStreamActive;
    Initialized                   = true;
    OutDiagnostic.clear();
    return true;
}

void UntitledLifecycle::DispatchFixedStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* Payload) noexcept
{
    if (auto* Self = static_cast<UntitledLifecycle*>(Payload))
    {
        Self->OnFixedSimulationStep(FixedDeltaSeconds, FixedStepOrdinal);
    }
}

void UntitledLifecycle::DispatchPresentationStep(double InterpolationAlpha, double WallDeltaSeconds, void* Payload) noexcept
{
    if (auto* Self = static_cast<UntitledLifecycle*>(Payload))
    {
        Self->OnPresentationStep(InterpolationAlpha, WallDeltaSeconds);
    }
}

void UntitledLifecycle::OnFixedSimulationStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal) noexcept
{
    using namespace Frontier::SimulationSpace::Policies;

    // 1. Build compact 16-feature driving observation (9 clearance rays + 7 kinematic features)
    DrivingObservationVector Observation{};
    for (std::size_t Ray = 0u; Ray < 9u; ++Ray)
    {
        Observation.Features[Ray] = 0.85f; // Open forward corridor
    }
    const float Speed = std::sqrt(
        ReferenceVehicle.VelocityMetersSecX * ReferenceVehicle.VelocityMetersSecX +
        ReferenceVehicle.VelocityMetersSecY * ReferenceVehicle.VelocityMetersSecY);
    Observation.Features[9]  = std::clamp(Speed / 30.0f, 0.0f, 1.0f);
    Observation.Features[10] = std::sin(ReferenceVehicle.YawRadians);
    Observation.Features[11] = std::cos(ReferenceVehicle.YawRadians);
    Observation.Features[12] = 0.0f;
    Observation.Features[13] = 0.5f;
    Observation.Features[14] = 0.0f;
    Observation.Features[15] = 1.0f;

    // 2. Evaluate deterministic 16->20->3 driving policy & round-trip through 4-byte wire quantization
    const DrivingControlOutput    RawControl  = Policies.DrivingPolicy.EvaluateDrivingControl(Observation);
    const QuantizedDrivingControl WireControl = DeterministicDrivingPolicy::QuantizeControl(RawControl);
    const DrivingControlOutput    Control     = DeterministicDrivingPolicy::DequantizeControl(WireControl);

    // 3. Validate control directive through GameplayRulePolicy authority bounds before integration
    OwnerControlDirective Directive{};
    Directive.SenderEndpointIndex = 1u;
    Directive.TargetActorId       = ReferenceVehicle.ActorIdentifier;
    Directive.SimulationTick      = FixedStepOrdinal;
    Directive.Steer               = Control.Steer;
    Directive.Throttle            = Control.Throttle;
    Directive.Brake               = Control.Brake;

    const DirectiveValidationOutcome Validation = GameplayRulePolicyEngine::ValidateOwnerDirective(
        Policies.GameplayPolicy,
        Directive,
        1u,
        Policies.GameplayPolicy.FixedSimulationHz);

    if (Validation == DirectiveValidationOutcome::Accepted)
    {
        ++Metrics.DirectivesAccepted;

        // Lean deterministic planar vehicle step (write your custom simulation here!)
        const float Acceleration = (Directive.Throttle * 6.5f) - (Directive.Brake * 9.0f) - (0.04f * Speed);
        const float NextSpeed    = std::clamp(Speed + Acceleration * static_cast<float>(FixedDeltaSeconds), 0.0f, 33.33f);
        ReferenceVehicle.YawRadians         += Directive.Steer * 0.45f * static_cast<float>(FixedDeltaSeconds);
        ReferenceVehicle.VelocityMetersSecX  = NextSpeed * std::cos(ReferenceVehicle.YawRadians);
        ReferenceVehicle.VelocityMetersSecY  = NextSpeed * std::sin(ReferenceVehicle.YawRadians);
        ReferenceVehicle.PositionMetersX    += static_cast<double>(ReferenceVehicle.VelocityMetersSecX) * FixedDeltaSeconds;
        ReferenceVehicle.PositionMetersY    += static_cast<double>(ReferenceVehicle.VelocityMetersSecY) * FixedDeltaSeconds;
        Metrics.VehicleSpeedMetersSec        = NextSpeed;
    }
    else
    {
        ++Metrics.DirectivesRejected;
    }

    // 4. Advance networking replication publish timer & compute deterministic 64-bit world alignment hash
    Frontier::NetworkingSpace::AccumulateReplicationElapsed(Policies.VehicleContract, FixedDeltaSeconds);

    ++Metrics.TotalFixedStepsExecuted;
    Metrics.SimulatedElapsedSeconds += FixedDeltaSeconds;
    Metrics.VehiclePositionMetersX   = ReferenceVehicle.PositionMetersX;
    Metrics.VehiclePositionMetersY   = ReferenceVehicle.PositionMetersY;

    const uint32_t HashInterval = std::max(1u, Policies.GameplayPolicy.AlignmentHashIntervalTicks);
    if ((FixedStepOrdinal % static_cast<uint64_t>(HashInterval)) == 0ull || FixedStepOrdinal == 1ull)
    {
        const std::array<WorldKinematicsDigestEntry, 1u> Snapshot = { ReferenceVehicle };
        Metrics.LastWorldAlignmentHash = GameplayRulePolicyEngine::ComputeWorldAlignmentHash(FixedStepOrdinal, Snapshot);
        ++Metrics.AlignmentHashesComputed;
    }
}

void UntitledLifecycle::OnPresentationStep(double InterpolationAlpha, double /*WallDeltaSeconds*/) noexcept
{
    ++Metrics.PresentationTicksCount;
    Metrics.LastInterpolationAlpha = InterpolationAlpha;
}

uint32_t UntitledLifecycle::RunBoundedTicks(uint32_t MaximumOuterTicks, double WallStepSeconds) noexcept
{
    if (!Initialized)
    {
        return 0u;
    }
    return RuntimeLoop.ExecuteBoundedFixedLoop(
        MaximumOuterTicks,
        WallStepSeconds,
        &UntitledLifecycle::DispatchFixedStep,
        &UntitledLifecycle::DispatchPresentationStep,
        this);
}

bool UntitledLifecycle::VerifyPolicyContracts(std::string& OutDiagnostic) const
{
    using namespace Frontier::SimulationSpace::Policies;

    // 1. GameplayRulePolicy < 128B wire round-trip
    std::array<std::byte, GameplayRulePolicy::WirePayloadBytes> RuleWire{};
    if (!GameplayRulePolicyEngine::EncodePolicyWireBytes(Policies.GameplayPolicy, RuleWire))
    {
        OutDiagnostic = "GameplayRulePolicy wire encode failed.";
        return false;
    }
    GameplayRulePolicy DecodedRule{};
    if (!GameplayRulePolicyEngine::DecodePolicyWireBytes(RuleWire, DecodedRule, OutDiagnostic))
    {
        return false;
    }
    if (DecodedRule.FixedSimulationHz != Policies.GameplayPolicy.FixedSimulationHz)
    {
        OutDiagnostic = "GameplayRulePolicy wire FixedSimulationHz mismatch.";
        return false;
    }

    // 2. DeterministicDrivingPolicy 1,612 B DocumentContainer round-trip
    Frontier::Serialization::DocumentContainer PolicyDoc{};
    if (!Policies.DrivingPolicy.EncodeToDocument("UntitledAutonomousDriver", PolicyDoc))
    {
        OutDiagnostic = "DeterministicDrivingPolicy encode to DocumentContainer failed.";
        return false;
    }
    std::vector<std::byte> WireContainerBytes{};
    if (!Frontier::Serialization::DocumentContainerEncoder::EncodeToBytes(PolicyDoc, WireContainerBytes, OutDiagnostic))
    {
        return false;
    }
    Frontier::Serialization::DocumentContainer DecodedDoc{};
    if (!Frontier::Serialization::DocumentContainerDecoder::DecodeFromBytes(WireContainerBytes, DecodedDoc, OutDiagnostic))
    {
        return false;
    }
    DeterministicDrivingPolicy RestoredDriver{};
    if (!RestoredDriver.DecodeFromDocument(DecodedDoc, OutDiagnostic))
    {
        return false;
    }
    if (RestoredDriver.ComputeParameterDigest() != Policies.DrivingPolicy.ComputeParameterDigest())
    {
        OutDiagnostic = "DeterministicDrivingPolicy parameter digest diverged across DocumentContainer round-trip.";
        return false;
    }

    OutDiagnostic.clear();
    return true;
}

bool UntitledLifecycle::Suspend() noexcept
{
    return Initialized && RuntimeLoop.Suspend();
}

bool UntitledLifecycle::Resume() noexcept
{
    return Initialized && RuntimeLoop.Resume();
}

void UntitledLifecycle::Finalize() noexcept
{
    RuntimeLoop.Finalize();
    Initialized = false;
}

UntitledLoopMetrics UntitledLifecycle::InspectMetrics() const noexcept
{
    return Metrics;
}

} // namespace Untitled
