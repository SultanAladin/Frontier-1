/*============================================================================================================================================
                                                  CROSSTHREADCONSTRAINTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation of generic PublishedReadingExchange, ThreadBoundaryExchange, PhysicsThread, and all 8 thread-safe Jolt constraint kinds

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/Threading/Sync/PublishedReadingExchange.h"
#include "Engine/Threading/Sync/ThreadBoundaryExchange.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Threads/Physics/PhysicsThread.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::ComponentClass;
using namespace Frontier::SimulationSpace::PhysicsSpace;
using namespace Frontier::Threading;

bool Require(bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::printf("[PASS] %s\n", Label);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    VALIDATION 1: GENERIC CROSS-THREAD READING EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

struct TelemetryProbeReading
{
    uint64_t              SequenceTag = 0ull; // [count] - Published generation fingerprint
    uint64_t              Checksum    = 0ull; // [hash]  - Exact sum of SampleSeries proving zero torn reads
    std::vector<uint64_t> SampleSeries {};    // [-]     - Dynamic contiguous series verifying safe container reuse
};

struct TelemetryProbeDirective
{
    uint32_t ProducerLane = 0u;   // [idx]   - Originating caller thread index
    uint64_t PayloadStamp = 0ull; // [count] - Unique directive payload counter
};

bool ValidateGenericCrossThreadExchange()
{
    ThreadBoundaryExchange<TelemetryProbeReading, TelemetryProbeDirective, 8u> Boundary(2048u);

    constexpr uint32_t ProducerStepCount = 1500u;
    constexpr uint32_t ReaderThreadCount = 4u;
    constexpr uint32_t DirectivesPerReader = 200u;

    std::atomic<bool>     ProducerFinished { false };
    std::atomic<uint64_t> TornReadFailures { 0ull };
    std::atomic<uint64_t> MonotonicFailures { 0ull };
    std::atomic<uint64_t> TotalReadsCompleted { 0ull };
    std::atomic<uint64_t> DrainedChecksum { 0ull };
    std::atomic<uint64_t> DrainedCount { 0ull };

    // Hold a pinned historical lease across many producer generations to validate ABA-free slot protection
    auto PinnedInitialLease = Boundary.AcquireReadLease();
    if (!Require(PinnedInitialLease.Valid() && PinnedInitialLease.InspectHeader().GenerationSequence == 0ull,
                 "Generic PublishedReadingExchange provides valid seed lease at generation 0"))
    {
        return false;
    }

    std::vector<std::thread> ReaderThreads;
    ReaderThreads.reserve(ReaderThreadCount);
    for (uint32_t Lane = 0u; Lane < ReaderThreadCount; ++Lane)
    {
        ReaderThreads.emplace_back([&, Lane]()
        {
            uint64_t PreviousGen = 0ull;
            uint32_t SentDirectives = 0u;
            while (!ProducerFinished.load(std::memory_order_acquire) || SentDirectives < DirectivesPerReader)
            {
                if (SentDirectives < DirectivesPerReader)
                {
                    TelemetryProbeDirective Dir {};
                    Dir.ProducerLane = Lane;
                    Dir.PayloadStamp = static_cast<uint64_t>(Lane + 1u) * 10000ull + SentDirectives;
                    if (Boundary.EnqueueDirective(Dir))
                    {
                        ++SentDirectives;
                    }
                }

                const auto Lease = Boundary.AcquireReadLease();
                const ReadingEpochHeader Header = Lease.InspectHeader();
                if (Header.GenerationSequence < PreviousGen)
                {
                    MonotonicFailures.fetch_add(1ull, std::memory_order_relaxed);
                }
                PreviousGen = Header.GenerationSequence;

                const TelemetryProbeReading& Snap = Lease.InspectPayload();
                if (Snap.SequenceTag != Header.GenerationSequence)
                {
                    TornReadFailures.fetch_add(1ull, std::memory_order_relaxed);
                }
                uint64_t Sum = 0ull;
                for (const uint64_t Sample : Snap.SampleSeries)
                {
                    Sum += Sample;
                }
                if (Sum != Snap.Checksum)
                {
                    TornReadFailures.fetch_add(1ull, std::memory_order_relaxed);
                }

                TotalReadsCompleted.fetch_add(1ull, std::memory_order_relaxed);
            }
        });
    }

    for (uint32_t Step = 1u; Step <= ProducerStepCount; ++Step)
    {
        (void)Boundary.DrainDirectives([&](const TelemetryProbeDirective& Incoming)
        {
            DrainedChecksum.fetch_add(Incoming.PayloadStamp, std::memory_order_relaxed);
            DrainedCount.fetch_add(1ull, std::memory_order_relaxed);
        });

        const bool Published = Boundary.PublishReadingInPlace(
            [Step](TelemetryProbeReading& Writable) noexcept
            {
                Writable.SequenceTag = Step;
                const std::size_t Count = 16u + (Step % 48u);
                Writable.SampleSeries.resize(Count);
                uint64_t Sum = 0ull;
                for (std::size_t I = 0u; I < Count; ++I)
                {
                    const uint64_t Term = static_cast<uint64_t>(Step) * 31ull + static_cast<uint64_t>(I) * 7ull;
                    Writable.SampleSeries[I] = Term;
                    Sum += Term;
                }
                Writable.Checksum = Sum;
            },
            static_cast<double>(Step) / 60.0,
            Step);

        if (!Published)
        {
            ProducerFinished.store(true, std::memory_order_release);
            for (auto& Thread : ReaderThreads) Thread.join();
            return Require(false, "Generic PublishedReadingExchange published all producer steps without starvation");
        }
    }

    ProducerFinished.store(true, std::memory_order_release);
    for (auto& Thread : ReaderThreads)
    {
        Thread.join();
    }

    // Drain any final directives queued while readers exited
    (void)Boundary.DrainDirectives([&](const TelemetryProbeDirective& Incoming)
    {
        DrainedChecksum.fetch_add(Incoming.PayloadStamp, std::memory_order_relaxed);
        DrainedCount.fetch_add(1ull, std::memory_order_relaxed);
    });

    // Verify pinned lease stayed completely uncorrupted through 1500 publications
    const bool PinnedUntouched = (PinnedInitialLease->SequenceTag == 0ull
        && PinnedInitialLease->SampleSeries.empty()
        && PinnedInitialLease->Checksum == 0ull);
    PinnedInitialLease.Release();

    uint64_t ExpectedChecksum = 0ull;
    for (uint32_t Lane = 0u; Lane < ReaderThreadCount; ++Lane)
    {
        for (uint32_t K = 0u; K < DirectivesPerReader; ++K)
        {
            ExpectedChecksum += static_cast<uint64_t>(Lane + 1u) * 10000ull + K;
        }
    }

    const bool OkTorn      = Require(TornReadFailures.load() == 0ull, "Generic PublishedReadingExchange zero torn reads across 4 concurrent reader threads");
    const bool OkMono      = Require(MonotonicFailures.load() == 0ull, "Generic PublishedReadingExchange strict monotonic generation per reader");
    const bool OkPinned    = Require(PinnedUntouched, "Pinned historical ReadingLease remained immune to 1500 producer wraps");
    const bool OkDirective = Require(
        DrainedCount.load() == static_cast<uint64_t>(ReaderThreadCount) * DirectivesPerReader
        && DrainedChecksum.load() == ExpectedChecksum,
        "Generic ThreadBoundaryExchange delivered 800/800 cross-thread directives with exact checksum");
    const bool OkCount     = Require(
        Boundary.QueryPublishedGeneration() == ProducerStepCount && TotalReadsCompleted.load() >= ReaderThreadCount * 50ull,
        "Generic PublishedReadingExchange completed high-throughput concurrent read stress");

    return OkTorn && OkMono && OkPinned && OkDirective && OkCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                               VALIDATION 2: COMPONENT CONVERSION & ALL 8 CONSTRAINT KINDS
//------------------------------------------------------------------------------------------------------------------------

bool ValidateEightConstraintSuite()
{
    // Verify ProjectZeroEditor Dock key matching semantics
    if (!Require(
            ConstraintSolver::VerifyDockKeyAccepted("", "anything")
            && ConstraintSolver::VerifyDockKeyAccepted("RearWing", "aero, rearwing , carbon")
            && !ConstraintSolver::VerifyDockKeyAccepted("RearWing", "bumper, front"),
            "ConstraintSolver::VerifyDockKeyAccepted matches ProjectZeroEditor key/tag rules"))
    {
        return false;
    }

    PhysicsSimulationParameters Parameters {};
    Parameters.GravityMps2 = { 0.0f, -9.81f, 0.0f }; // Y-up world
    Parameters.FixedStepHz = 60.0f;

    PhysicsExtension Physics(Parameters);
    if (!Require(Physics.Initialize(), "PhysicsExtension::Initialize for 8-kind constraint suite"))
    {
        return false;
    }

    auto SpawnStatic = [&](Mathematics::Axis3D Pos) -> RigidBodyToken
    {
        RigidBodyDescriptor Desc {};
        Desc.MotionCategory          = RigidMotionCategory::Static;
        Desc.PositionMeters          = Pos;
        Desc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        Desc.Shape.HalfExtentsMeters = { 0.15f, 0.15f, 0.15f };
        return Physics.IssueRigidBody(Desc);
    };
    auto SpawnDynamic = [&](Mathematics::Axis3D Pos, float MassKg = 4.0f) -> RigidBodyToken
    {
        RigidBodyDescriptor Desc {};
        Desc.MotionCategory          = RigidMotionCategory::Dynamic;
        Desc.PositionMeters          = Pos;
        Desc.ExplicitMassKg          = MassKg;
        Desc.LinearDamping           = 0.05f;
        Desc.AngularDamping          = 0.05f;
        Desc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        Desc.Shape.HalfExtentsMeters = { 0.15f, 0.15f, 0.15f };
        return Physics.IssueRigidBody(Desc);
    };

    // 1. Fixed constraint built via ComponentClass::ConstraintComponentSettings conversion
    const RigidBodyToken FixedA = SpawnStatic({ 0.0f, 5.0f, 0.0f });
    const RigidBodyToken FixedB = SpawnDynamic({ 0.45f, 5.0f, 0.0f }, 3.0f);
    ConstraintComponentSettings FixedSettings {};
    FixedSettings.Form              = PhysicsConstraintCategory::Fixed;
    FixedSettings.BreakForceNewtons = 0.0f;
    const ConstraintToken FixedTok = Physics.IssueConstraint(
        ConstraintSolver::ConstructDescriptorFromComponent(FixedSettings, FixedA, FixedB, { 0.22f, 5.0f, 0.0f }, {}, 101ull));

    // 2. Hinge (Sweep mode between -0.5..+0.5 rad)
    const RigidBodyToken HingeA = SpawnStatic({ 4.0f, 5.0f, 0.0f });
    const RigidBodyToken HingeB = SpawnDynamic({ 4.45f, 5.0f, 0.0f }, 3.0f);
    ConstraintDescriptor HingeDesc {};
    HingeDesc.Category              = ConstraintCategory::Hinge;
    HingeDesc.FirstBody             = HingeA;
    HingeDesc.SecondBody            = HingeB;
    HingeDesc.AnchorWorldMeters     = { 4.0f, 5.0f, 0.0f };
    HingeDesc.AxisUnit              = { 0.0f, 1.0f, 0.0f };
    HingeDesc.MinLimit              = -0.50f;
    HingeDesc.MaxLimit              = 0.50f;
    HingeDesc.DriveCategory         = ConstraintDriveCategory::Sweep;
    HingeDesc.CyclePeriodSeconds    = 1.0f;
    HingeDesc.MotorDriveEnabled     = true;
    HingeDesc.MaxMotorTorqueOrForce = 250.0f;
    HingeDesc.ComponentInstanceTag  = 102ull;
    const ConstraintToken HingeTok  = Physics.IssueConstraint(HingeDesc);

    // 3. Slider (motorized position target along +X)
    const RigidBodyToken SliderA = SpawnStatic({ 8.0f, 5.0f, 0.0f });
    const RigidBodyToken SliderB = SpawnDynamic({ 8.5f, 5.0f, 0.0f }, 3.0f);
    ConstraintDescriptor SliderDesc {};
    SliderDesc.Category              = ConstraintCategory::Slider;
    SliderDesc.FirstBody             = SliderA;
    SliderDesc.SecondBody            = SliderB;
    SliderDesc.AnchorWorldMeters     = { 8.5f, 5.0f, 0.0f };
    SliderDesc.AxisUnit              = { 1.0f, 0.0f, 0.0f };
    SliderDesc.MinLimit              = -0.40f;
    SliderDesc.MaxLimit              = 0.40f;
    SliderDesc.DriveCategory         = ConstraintDriveCategory::MotorPosition;
    SliderDesc.MotorDriveEnabled     = true;
    SliderDesc.MaxMotorTorqueOrForce = 800.0f;
    SliderDesc.TargetMotorCoordinate = 0.25f;
    SliderDesc.ComponentInstanceTag  = 103ull;
    const ConstraintToken SliderTok  = Physics.IssueConstraint(SliderDesc);

    // 4. Ball (SwingTwist with cone + twist decomposition)
    const RigidBodyToken BallA = SpawnStatic({ 12.0f, 5.0f, 0.0f });
    const RigidBodyToken BallB = SpawnDynamic({ 12.45f, 4.6f, 0.0f }, 2.0f);
    ConstraintDescriptor BallDesc {};
    BallDesc.Category          = ConstraintCategory::SwingTwist;
    BallDesc.FirstBody         = BallA;
    BallDesc.SecondBody        = BallB;
    BallDesc.AnchorWorldMeters = { 12.0f, 5.0f, 0.0f };
    BallDesc.AxisUnit          = { 0.0f, -1.0f, 0.0f };
    BallDesc.ConeHalfAngleRad  = 0.55f;
    BallDesc.TwistHalfAngleRad = 0.65f;
    BallDesc.ComponentInstanceTag = 104ull;
    const ConstraintToken BallTok = Physics.IssueConstraint(BallDesc);

    // 5. Spring (2.2 Hz, damping 0.35, +-0.12 m)
    const RigidBodyToken SpringA = SpawnStatic({ 15.5f, 5.0f, 0.0f });
    const RigidBodyToken SpringB = SpawnDynamic({ 16.0f, 5.0f, 0.0f }, 10.0f);
    ConstraintDescriptor SpringDesc {};
    SpringDesc.Category              = ConstraintCategory::Spring;
    SpringDesc.FirstBody             = SpringA;
    SpringDesc.SecondBody            = SpringB;
    SpringDesc.AnchorWorldMeters     = { 16.0f, 5.0f, 0.0f };
    SpringDesc.AxisUnit              = { 0.0f, 1.0f, 0.0f };
    SpringDesc.MinLimit              = -0.12f;
    SpringDesc.MaxLimit              = 0.12f;
    SpringDesc.SpringFrequencyHz     = 2.2f;
    SpringDesc.SpringDampingRatio    = 0.35f;
    SpringDesc.MaxMotorTorqueOrForce = 5000.0f;
    SpringDesc.ComponentInstanceTag  = 105ull;
    const ConstraintToken SpringTok  = Physics.IssueConstraint(SpringDesc);

    // 6. Spin (LinearSpeed source: 10 m/s over 0.5 m radius -> 20 rad/s)
    const RigidBodyToken SpinA = SpawnStatic({ 20.0f, 5.0f, -0.5f });
    const RigidBodyToken SpinB = SpawnDynamic({ 20.0f, 5.0f, 0.0f }, 2.0f);
    ConstraintDescriptor SpinDesc {};
    SpinDesc.Category              = ConstraintCategory::Spin;
    SpinDesc.FirstBody             = SpinA;
    SpinDesc.SecondBody            = SpinB;
    SpinDesc.AnchorWorldMeters     = { 20.0f, 5.0f, 0.0f };
    SpinDesc.AxisUnit              = { 0.0f, 0.0f, 1.0f };
    SpinDesc.SpinSource            = SpinRateSourceCategory::LinearSpeed;
    SpinDesc.RollingRadiusMeters   = 0.5f;
    SpinDesc.MotorDriveEnabled     = true;
    SpinDesc.MotorVelocityMode     = true;
    SpinDesc.MaxMotorTorqueOrForce = 150.0f;
    SpinDesc.ComponentInstanceTag  = 106ull;
    const ConstraintToken SpinTok  = Physics.IssueConstraint(SpinDesc);

    // Submit linear vehicle speed directive (10 m/s) across thread boundary
    ConstraintDirective SpinSpeedDir {};
    SpinSpeedDir.Category        = ConstraintDirectiveCategory::AssignSpinLinearSpeed;
    SpinSpeedDir.TargetToken     = SpinTok;
    SpinSpeedDir.PrimaryScalar   = 10.0f;
    SpinSpeedDir.SecondaryScalar = 0.5f;
    (void)Physics.SubmitConstraintDirective(SpinSpeedDir);

    // 7. Tether (unilateral 0.60 m rope, starting slack at 0.30 m then falling taut)
    const RigidBodyToken TetherA = SpawnStatic({ 24.0f, 5.0f, 0.0f });
    const RigidBodyToken TetherB = SpawnDynamic({ 24.0f, 4.70f, 0.0f }, 3.0f);
    ConstraintDescriptor TetherDesc {};
    TetherDesc.Category           = ConstraintCategory::Tether;
    TetherDesc.FirstBody          = TetherA;
    TetherDesc.SecondBody         = TetherB;
    TetherDesc.AnchorWorldMeters  = { 24.0f, 5.0f, 0.0f };
    TetherDesc.SecondAnchorMeters = { 24.0f, 4.70f, 0.0f };
    TetherDesc.AxisUnit           = { 0.0f, 1.0f, 0.0f };
    TetherDesc.MaxLimit           = 0.60f;
    TetherDesc.ComponentInstanceTag = 107ull;
    const ConstraintToken TetherTok = Physics.IssueConstraint(TetherDesc);

    // 8. Dock (key-filtered magnetic latch with PullTest rupture & re-latching)
    const RigidBodyToken DockA = SpawnStatic({ 27.5f, 5.0f, 0.0f });
    const RigidBodyToken DockB = SpawnDynamic({ 28.0f, 5.0f, 0.0f }, 4.0f);
    ConstraintDescriptor DockDesc {};
    DockDesc.Category               = ConstraintCategory::Dock;
    DockDesc.FirstBody              = DockA;
    DockDesc.SecondBody             = DockB;
    DockDesc.AnchorWorldMeters      = { 28.0f, 5.0f, 0.0f };
    DockDesc.BreakForceN            = 1000.0f;
    DockDesc.SnapRadiusMeters       = 0.40f;
    DockDesc.DriveCategory          = ConstraintDriveCategory::PullTest;
    DockDesc.DockKeyHash            = ConstraintSolver::ComputeDockKeyHash("rearwing");
    DockDesc.DockKeyMatched         = true;
    DockDesc.DockInitiallyConnected = true;
    DockDesc.ComponentInstanceTag   = 108ull;
    const ConstraintToken DockTok   = Physics.IssueConstraint(DockDesc);

    // 8b. Rejected Dock (wrong key initially detached inside SnapRadius)
    const RigidBodyToken RejectA = SpawnStatic({ 31.5f, 5.0f, 0.0f });
    const RigidBodyToken RejectB = SpawnDynamic({ 32.0f, 5.0f, 0.0f }, 4.0f);
    ConstraintDescriptor RejectDesc {};
    RejectDesc.Category               = ConstraintCategory::Dock;
    RejectDesc.FirstBody              = RejectA;
    RejectDesc.SecondBody             = RejectB;
    RejectDesc.AnchorWorldMeters      = { 32.0f, 5.0f, 0.0f };
    RejectDesc.SnapRadiusMeters       = 0.50f;
    RejectDesc.DockKeyHash            = ConstraintSolver::ComputeDockKeyHash("trailer");
    RejectDesc.DockKeyMatched         = false;
    RejectDesc.DockInitiallyConnected = true;
    RejectDesc.ComponentInstanceTag   = 109ull;
    const ConstraintToken RejectTok   = Physics.IssueConstraint(RejectDesc);

    if (!Require(
            FixedTok.Valid() && HingeTok.Valid() && SliderTok.Valid() && BallTok.Valid()
            && SpringTok.Valid() && SpinTok.Valid() && TetherTok.Valid() && DockTok.Valid() && RejectTok.Valid(),
            "All 8 ProjectZero-parity constraint kinds + rejected Dock issue valid tokens"))
    {
        return false;
    }

    // Verify Tether starts slack before falling
    {
        const auto InitialLease = Physics.AcquireConstraintReading();
        const ConstraintReading* TetherStart = InitialLease->ResolveConstraint(TetherTok);
        if (!Require(TetherStart != nullptr && !TetherStart->TautEngaged && TetherStart->CurrentCoordinate < 0.40f,
                     "Tether starts slack (< 0.40 m) in published initial reading"))
        {
            return false;
        }
    }

    // Step 180 ticks (3.0 s) so Sweep oscillates, Slider settles, Spin reaches 20 rad/s, Tether goes taut, PullTest breaks Dock
    float HingeMinSeen = 10.0f;
    float HingeMaxSeen = -10.0f;
    bool  DockReleaseEventObserved = false;
    for (int Step = 0; Step < 180; ++Step)
    {
        Physics.Tick(1.0 / 60.0);
        const auto Lease = Physics.AcquireConstraintReading();
        if (const ConstraintReading* H = Lease->ResolveConstraint(HingeTok))
        {
            HingeMinSeen = std::min(HingeMinSeen, H->CurrentCoordinate);
            HingeMaxSeen = std::max(HingeMaxSeen, H->CurrentCoordinate);
        }
        for (const ConstraintTransitionEvent& Ev : Lease->InspectStepEvents())
        {
            if (Ev.Token == DockTok && Ev.Transition == ConstraintTransitionCategory::DockReleased)
            {
                DockReleaseEventObserved = true;
            }
        }
    }

    const auto SnapLease = Physics.AcquireConstraintReading();
    const ConstraintReading* FixedR  = SnapLease->ResolveByComponentTag(101ull);
    const ConstraintReading* SliderR = SnapLease->ResolveConstraint(SliderTok);
    const ConstraintReading* BallR   = SnapLease->ResolveConstraint(BallTok);
    const ConstraintReading* SpringR = SnapLease->ResolveConstraint(SpringTok);
    const ConstraintReading* SpinR   = SnapLease->ResolveConstraint(SpinTok);
    const ConstraintReading* TetherR = SnapLease->ResolveConstraint(TetherTok);
    const ConstraintReading* DockR   = SnapLease->ResolveConstraint(DockTok);
    const ConstraintReading* RejectR = SnapLease->ResolveConstraint(RejectTok);

    bool AllOk = true;
    AllOk &= Require(SnapLease->QueryConstraintCount() == 9u, "ConstraintListReading exposes complete contiguous list of 9 constraints");
    AllOk &= Require(FixedR != nullptr && FixedR->Intact && FixedR->CurrentCoordinate < 0.05f, "Fixed constraint holds weld and resolves via ComponentInstanceTag");
    AllOk &= Require((HingeMaxSeen - HingeMinSeen) > 0.55f, "Hinge Sweep mode oscillates across configured angular limits");
    AllOk &= Require(SliderR != nullptr && SliderR->Intact && std::abs(SliderR->CurrentCoordinate - 0.25f) < 0.04f, "Motorized Slider tracks 0.25 m target");
    AllOk &= Require(BallR != nullptr && BallR->Intact && BallR->SwingAngleRad > 0.01f && BallR->SwingAngleRad <= 0.70f, "Ball SwingTwist reports bounded cone swing angle");
    AllOk &= Require(SpringR != nullptr && SpringR->Intact && SpringR->CurrentCoordinate <= 0.0f && SpringR->CurrentCoordinate >= -0.125f, "Spring sags under gravity within +-0.12 m stroke");
    AllOk &= Require(SpinR != nullptr && SpinR->Intact && std::abs(SpinR->CurrentVelocity) > 15.0f && std::abs(SpinR->CurrentRpm) > 140.0f, "Spin LinearSpeed mode drives ~20 rad/s (~191 rpm) from 10 m/s over 0.5 m radius");
    AllOk &= Require(TetherR != nullptr && TetherR->Intact && TetherR->TautEngaged && TetherR->CurrentCoordinate >= 0.58f && TetherR->CurrentCoordinate <= 0.65f, "Tether transitions from slack to taut at 0.60 m limit");
    AllOk &= Require(DockR != nullptr && !DockR->Intact && DockR->DockPhase == DockLatchPhase::Released && DockReleaseEventObserved, "Dock PullTest ruptures past BreakForceN and emits DockReleased event");
    AllOk &= Require(RejectR != nullptr && !RejectR->Intact && RejectR->DockPhase == DockLatchPhase::Rejected, "Key-mismatched Dock refuses engagement in DockLatchPhase::Rejected");

    // Test Dock re-latching: bring DockB back within SnapRadiusMeters, switch to Hold, and submit EngageDock directive
    Physics.TeleportBody(DockB, { 28.0f, 5.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f });
    Physics.AssignBodyVelocity(DockB, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });

    ConstraintDirective HoldDir {};
    HoldDir.Category      = ConstraintDirectiveCategory::AssignDriveCategory;
    HoldDir.TargetToken   = DockTok;
    HoldDir.DriveCategory = ConstraintDriveCategory::Hold;
    (void)Physics.SubmitConstraintDirective(HoldDir);

    ConstraintDirective EngageDir {};
    EngageDir.Category      = ConstraintDirectiveCategory::EngageDock;
    EngageDir.TargetToken   = DockTok;
    EngageDir.ToggleEnabled = false; // Allow proximity snap within SnapRadiusMeters
    (void)Physics.SubmitConstraintDirective(EngageDir);

    bool DockReconnectedEvent = false;
    for (int Step = 0; Step < 5; ++Step)
    {
        Physics.Tick(1.0 / 60.0);
        const auto PostSnap = Physics.AcquireConstraintReading();
        for (const ConstraintTransitionEvent& Ev : PostSnap->InspectStepEvents())
        {
            if (Ev.Token == DockTok && Ev.Transition == ConstraintTransitionCategory::DockConnected)
            {
                DockReconnectedEvent = true;
            }
        }
    }

    const ConstraintReading DockAfterRelatch = Physics.InspectConstraintReading(DockTok);
    AllOk &= Require(
        DockAfterRelatch.Intact && DockAfterRelatch.DockPhase == DockLatchPhase::Connected && DockReconnectedEvent,
        "Dock auto-relatches within SnapRadiusMeters with matching key and emits DockConnected event");

    Physics.Finalize();
    return AllOk;
}

//------------------------------------------------------------------------------------------------------------------------
//                      VALIDATION 3: LIVE DEDICATED PHYSICSTHREAD + CONCURRENT MULTI-THREAD READERS
//------------------------------------------------------------------------------------------------------------------------

void PhysicsThreadStepTrampoline(void* OpaqueHost, double FixedDeltaSeconds) noexcept
{
    auto* Physics = static_cast<PhysicsExtension*>(OpaqueHost);
    if (Physics != nullptr)
    {
        Physics->Tick(FixedDeltaSeconds);
    }
}

bool ValidateLivePhysicsThreadMultiReaders()
{
    PhysicsSimulationParameters Parameters {};
    Parameters.GravityMps2 = { 0.0f, -9.81f, 0.0f };
    Parameters.FixedStepHz = 120.0f;

    PhysicsExtension Physics(Parameters);
    if (!Require(Physics.Initialize(), "PhysicsExtension::Initialize for live PhysicsThread stress"))
    {
        return false;
    }

    // Build a multi-constraint rig on the owner thread prior to handing stepping to PhysicsThread
    std::vector<ConstraintToken> RigTokens;
    for (int I = 0; I < 6; ++I)
    {
        RigidBodyDescriptor AnchorDesc {};
        AnchorDesc.MotionCategory          = RigidMotionCategory::Static;
        AnchorDesc.PositionMeters          = { static_cast<float>(I) * 3.0f, 6.0f, 0.0f };
        AnchorDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        AnchorDesc.Shape.HalfExtentsMeters = { 0.15f, 0.15f, 0.15f };
        const RigidBodyToken Anchor = Physics.IssueRigidBody(AnchorDesc);

        RigidBodyDescriptor BodyDesc {};
        BodyDesc.MotionCategory          = RigidMotionCategory::Dynamic;
        BodyDesc.PositionMeters          = { static_cast<float>(I) * 3.0f + 0.45f, 6.0f, 0.0f };
        BodyDesc.ExplicitMassKg          = 4.0f;
        BodyDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        BodyDesc.Shape.HalfExtentsMeters = { 0.15f, 0.15f, 0.15f };
        const RigidBodyToken DynamicBody = Physics.IssueRigidBody(BodyDesc);

        ConstraintDescriptor Desc {};
        Desc.FirstBody             = Anchor;
        Desc.SecondBody            = DynamicBody;
        Desc.AnchorWorldMeters     = { static_cast<float>(I) * 3.0f, 6.0f, 0.0f };
        Desc.AxisUnit              = { 0.0f, 1.0f, 0.0f };
        Desc.MinLimit              = -0.6f;
        Desc.MaxLimit              = 0.6f;
        Desc.MotorDriveEnabled     = true;
        Desc.MaxMotorTorqueOrForce = 250.0f;
        Desc.ComponentInstanceTag  = static_cast<uint64_t>(500 + I);
        Desc.Category              = (I % 2 == 0) ? ConstraintCategory::Hinge : ConstraintCategory::Spring;
        if (Desc.Category == ConstraintCategory::Spring)
        {
            Desc.AnchorWorldMeters  = BodyDesc.PositionMeters;
            Desc.MinLimit           = -0.10f;
            Desc.MaxLimit           = 0.10f;
            Desc.SpringFrequencyHz  = 2.5f;
            Desc.SpringDampingRatio = 0.40f;
        }
        RigTokens.push_back(Physics.IssueConstraint(Desc));
    }

    ThreadProgressionTracker Watchdog {};
    PhysicsThread            SimThread {};
    PhysicsThreadConfiguration ThreadCfg {};
    ThreadCfg.FixedStepHz            = 120.0f;
    ThreadCfg.SpawnDedicatedOsThread = true;
    ThreadCfg.PacedToWallClock       = false; // Uncapped stress stepping to maximize concurrent overlap
    ThreadCfg.StepCallback           = &PhysicsThreadStepTrampoline;
    ThreadCfg.OpaqueHost             = &Physics;

    if (!Require(SimThread.Initialize(ThreadCfg, &Watchdog), "PhysicsThread::Initialize with dedicated OS thread"))
    {
        Physics.Finalize();
        return false;
    }

    std::atomic<bool>     StopReaders { false };
    std::atomic<uint64_t> ReaderErrors { 0ull };
    std::atomic<uint64_t> GameThreadReads { 0ull };
    std::atomic<uint64_t> RenderThreadReads { 0ull };
    std::atomic<uint64_t> EditorThreadReads { 0ull };

    auto ValidateReadingBody = [&](const ConstraintListReading& Snap)
    {
        if (Snap.QueryConstraintCount() != RigTokens.size())
        {
            ReaderErrors.fetch_add(1ull, std::memory_order_relaxed);
            return;
        }
        for (const ConstraintReading& Reading : Snap.InspectConstraints())
        {
            if (!Reading.Resident
                || !std::isfinite(Reading.AnchorWorldMeters.X)
                || !std::isfinite(Reading.AttachedWorldMeters.Y)
                || !std::isfinite(Reading.JointWorldOrientation.W)
                || !std::isfinite(Reading.CurrentCoordinate))
            {
                ReaderErrors.fetch_add(1ull, std::memory_order_relaxed);
            }
        }
    };

    // GameThread: reads constraint reading list AND submits live Hinge steering directives
    std::thread GameThread([&]()
    {
        uint64_t LastGen = 0ull;
        uint32_t Iteration = 0u;
        while (!StopReaders.load(std::memory_order_acquire))
        {
            const auto Lease = Physics.AcquireConstraintReading();
            const uint64_t Gen = Lease.InspectHeader().GenerationSequence;
            if (Gen < LastGen)
            {
                ReaderErrors.fetch_add(1ull, std::memory_order_relaxed);
            }
            LastGen = Gen;
            ValidateReadingBody(*Lease);

            ConstraintDirective SteerDir {};
            SteerDir.Category      = ConstraintDirectiveCategory::AssignMotorTarget;
            SteerDir.TargetToken   = RigTokens[0u];
            SteerDir.PrimaryScalar = 0.35f * std::sin(static_cast<float>(Iteration) * 0.02f);
            (void)Physics.SubmitConstraintDirective(SteerDir);

            GameThreadReads.fetch_add(1ull, std::memory_order_relaxed);
            ++Iteration;
        }
    });

    // RenderThread: reads all attachment world poses concurrently
    std::thread RenderThread([&]()
    {
        while (!StopReaders.load(std::memory_order_acquire))
        {
            const auto Lease = Physics.AcquireConstraintReading();
            ValidateReadingBody(*Lease);
            RenderThreadReads.fetch_add(1ull, std::memory_order_relaxed);
        }
    });

    // EditorThread: resolves individual constraints by token & ComponentInstanceTag concurrently
    std::thread EditorThread([&]()
    {
        while (!StopReaders.load(std::memory_order_acquire))
        {
            const auto Lease = Physics.AcquireConstraintReading();
            for (std::size_t I = 0u; I < RigTokens.size(); ++I)
            {
                const ConstraintReading* ByToken = Lease->ResolveConstraint(RigTokens[I]);
                const ConstraintReading* ByTag   = Lease->ResolveByComponentTag(static_cast<uint64_t>(500u + I));
                if (ByToken == nullptr || ByTag == nullptr || ByToken != ByTag)
                {
                    ReaderErrors.fetch_add(1ull, std::memory_order_relaxed);
                }
            }
            EditorThreadReads.fetch_add(1ull, std::memory_order_relaxed);
        }
    });

    // Wait until PhysicsThread executes >= 120 Jolt steps AND all 3 reader threads overlap concurrently
    const auto StartWall = std::chrono::steady_clock::now();
    while (SimThread.QueryCompletedStepCount() < 120ull
        || GameThreadReads.load(std::memory_order_relaxed) < 200ull
        || RenderThreadReads.load(std::memory_order_relaxed) < 200ull
        || EditorThreadReads.load(std::memory_order_relaxed) < 200ull)
    {
        if (std::chrono::steady_clock::now() - StartWall > std::chrono::seconds(10))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    StopReaders.store(true, std::memory_order_release);
    GameThread.join();
    RenderThread.join();
    EditorThread.join();

    const uint64_t StepsExecuted = SimThread.QueryCompletedStepCount();
    SimThread.Finalize();
    Physics.Finalize();

    std::printf(
        "[INFO] PhysicsThreadSteps=%llu, GameReads=%llu, RenderReads=%llu, EditorReads=%llu, PublishedGen=%llu\n",
        static_cast<unsigned long long>(StepsExecuted),
        static_cast<unsigned long long>(GameThreadReads.load()),
        static_cast<unsigned long long>(RenderThreadReads.load()),
        static_cast<unsigned long long>(EditorThreadReads.load()),
        static_cast<unsigned long long>(Physics.QueryPublishedConstraintGeneration()));

    const bool OkSteps   = Require(StepsExecuted >= 120ull, "Dedicated PhysicsThread completed >= 120 concurrent Jolt fixed steps");
    const bool OkErrors  = Require(ReaderErrors.load() == 0ull, "Zero data races, torn reads, or invalid poses across Game/Render/Editor threads");
    const bool OkReaders = Require(
        GameThreadReads.load() > 100ull && RenderThreadReads.load() > 100ull && EditorThreadReads.load() > 100ull,
        "All 3 concurrent reader threads sustained wait-free reading reads during live Jolt simulation");

    return OkSteps && OkErrors && OkReaders;
}

} // namespace

int main()
{
    std::printf("=== CrossThreadConstraintValidation ===\n");
    if (!ValidateGenericCrossThreadExchange())
    {
        return 1;
    }
    if (!ValidateEightConstraintSuite())
    {
        return 1;
    }
    if (!ValidateLivePhysicsThreadMultiReaders())
    {
        return 1;
    }
    std::printf("ALL CrossThreadConstraintValidation CHECKS PASSED.\n");
    return 0;
}
