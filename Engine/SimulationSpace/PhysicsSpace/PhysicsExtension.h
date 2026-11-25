/*============================================================================================================================================
                                                            PHYSICSEXTENSION.H
============================================================================================================================================*/
// 🧩 Deterministic JoltPhysics v5.6.0 lifecycle participant satisfying SuspendableParticipant, constraints & collider proxies

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/CollisionDetection.h"
#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsDebugLog.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsMaterial.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"
#include "Engine/Threading/Sync/ThreadBoundaryExchange.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsSimulationParameters
{
    Mathematics::Axis3D GravityMps2         { 0.0f, 0.0f, -static_cast<float>(Constants::StandardGravity) }; // [m/s^2]
    float               FixedStepHz         = 60.0f; // [Hz]    - Fixed simulation substep rate
    uint32_t            MaxSubstepsPerTick  = 4u;    // [count] - Upper clamp on fixed substeps per Tick call
    uint32_t            SolverThreadCount   = 0u;    // [count] - 0 = single-threaded deterministic solver
    uint32_t            MaxRigidBodies      = 8192u; // [count] - Maximum concurrent rigid bodies
    uint32_t            TempAllocatorBytes  = 10u * 1024u * 1024u; // [B] - Preallocated scratch region for Jolt
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class PhysicsExtension
{
public:
    PhysicsExtension() noexcept;
    explicit PhysicsExtension(const PhysicsSimulationParameters& InitialParameters) noexcept;
    ~PhysicsExtension() noexcept;

    PhysicsExtension(const PhysicsExtension&)            = delete;
    PhysicsExtension& operator=(const PhysicsExtension&) = delete;
    PhysicsExtension(PhysicsExtension&&)                 = delete;
    PhysicsExtension& operator=(PhysicsExtension&&)      = delete;

    // Lifecycle::SuspendableParticipant interface
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool QueryInitialized() const noexcept;
    [[nodiscard]] bool QuerySuspended() const noexcept;

    // Simulation configuration
    void AssignGravity(const Mathematics::Axis3D& GravityMps2) noexcept;
    void ConfigureFixedStepHz(float FixedStepHz) noexcept;

    // Rigid body lifecycle & actuation
    [[nodiscard]] RigidBodyToken    IssueRigidBody(const RigidBodyDescriptor& Descriptor) noexcept;
    bool                            ReclaimRigidBody(RigidBodyToken Token) noexcept;
    [[nodiscard]] bool              AuthenticateBodyToken(RigidBodyToken Token) const noexcept;
    [[nodiscard]] RigidBodyReading InspectBodyReading(RigidBodyToken Token) const noexcept;

    void ApplyForce(RigidBodyToken Token, const Mathematics::Axis3D& ForceNewtons) noexcept;
    void ApplyForceAtPoint(
        RigidBodyToken             Token,
        const Mathematics::Axis3D& ForceNewtons,
        const Mathematics::Axis3D& WorldPointMeters) noexcept;
    void ApplyTorque(RigidBodyToken Token, const Mathematics::Axis3D& TorqueNm) noexcept;
    void AssignBodyMassKg(RigidBodyToken Token, float MassKg) noexcept;
    void ApplyLinearImpulse(RigidBodyToken Token, const Mathematics::Axis3D& ImpulseNs) noexcept;
    void ApplyImpulseAtPoint(
        RigidBodyToken             Token,
        const Mathematics::Axis3D& ImpulseNs,
        const Mathematics::Axis3D& WorldPointMeters) noexcept;
    void AssignBodyVelocity(
        RigidBodyToken             Token,
        const Mathematics::Axis3D& LinearVelocityMps,
        const Mathematics::Axis3D& AngularVelocityRadPerSec) noexcept;
    void TeleportBody(
        RigidBodyToken                    Token,
        const Mathematics::Axis3D&        PositionMeters,
        const RigidOrientationQuaternion& Orientation) noexcept;

    // Constraints & breakable strain bonds
    using ConstraintReadingLease    = Threading::ReadingLease<ConstraintListReading>;
    using ConstraintReadingExchange = Threading::PublishedReadingExchange<ConstraintListReading>;

    [[nodiscard]] ConstraintToken   IssueConstraint(const ConstraintDescriptor& Descriptor) noexcept;
    bool                            ReclaimConstraint(ConstraintToken Token) noexcept;
    bool                            BreakConstraint(ConstraintToken Token) noexcept;
    bool                            AssignConstraintMotorTarget(ConstraintToken Token, float TargetCoordinateOrVelocity) noexcept;
    [[nodiscard]] ConstraintReading InspectConstraintReading(ConstraintToken Token) const noexcept;

    // Wait-free cross-thread constraint reading reads & lock-free directive intake (safe across PhysicsThread, GameThread, RenderThread, EditorThread)
    [[nodiscard]] ConstraintReadingLease           AcquireConstraintReading() const noexcept;
    [[nodiscard]] bool                              SubmitConstraintDirective(const ConstraintDirective& Directive) noexcept;
    [[nodiscard]] const ConstraintReadingExchange& InspectConstraintReadingExchange() const noexcept;
    [[nodiscard]] uint64_t                          QueryPublishedConstraintGeneration() const noexcept;
    [[nodiscard]] uint32_t                          QueryLiveConstraintCount() const noexcept;

    // PhysicsMaterial registry (O(1) lookup on PhysicsThread)
    [[nodiscard]] uint16_t                            RegisterPhysicsMaterial(const PhysicsMaterialSpecification& Spec) noexcept;
    [[nodiscard]] const PhysicsMaterialSpecification& InspectPhysicsMaterial(uint16_t MaterialId) const noexcept;
    [[nodiscard]] uint16_t                            QueryBodyMaterialId(RigidBodyToken Token) const noexcept;

    // Wait-free cross-thread trigger overlap reading (Enter / Stay / Exit for IsTriggerSensor bodies)
    using TriggerOverlapReadingLease    = Threading::ReadingLease<TriggerOverlapListReading>;
    using TriggerOverlapReadingExchange = Threading::PublishedReadingExchange<TriggerOverlapListReading>;

    [[nodiscard]] TriggerOverlapReadingLease AcquireTriggerOverlapReading() const noexcept;
    [[nodiscard]] uint64_t                   QueryPublishedTriggerGeneration() const noexcept;

    // Queries, collider proxy export & contact telemetry
    [[nodiscard]] RayCastHitReading   ExecuteRayCast(const RayCastQuery& Query) const noexcept;
    [[nodiscard]] RayCastHitReading   ExecuteRayCastIgnoringBody(
        const RayCastQuery& Query,
        RigidBodyToken      IgnoredBody) const noexcept;
    [[nodiscard]] ShapeCastHitReading ExecuteShapeCast(const ShapeCastQuery& Query) const noexcept;
    [[nodiscard]] ShapeCastHitReading ExecuteShapeCastIgnoringBody(
        const ShapeCastQuery& Query,
        RigidBodyToken        IgnoredBody) const noexcept;
    void QuerySphereOverlaps(const SphereOverlapQuery& Query, std::vector<RigidBodyToken>& OutBodies) const;
    void QueryBoxOverlaps(const BoxOverlapQuery& Query, std::vector<RigidBodyToken>& OutBodies) const;
    void ExportRigidColliderProxies(
        std::vector<RigidColliderProxy>& OutProxies,
        uint32_t                         ChannelMask = 0xFFFFFFFFu) const;

    [[nodiscard]] std::span<const ContactImpulseReading> InspectRecentContacts() const noexcept;
    [[nodiscard]] uint32_t QueryLiveBodyCount() const noexcept;
    [[nodiscard]] uint32_t QueryActiveBodyCount() const noexcept;
    [[nodiscard]] uint32_t QueryIntactBondCount() const noexcept;
    [[nodiscard]] uint64_t QueryCompletedStepCount() const noexcept;
    [[nodiscard]] double   QuerySimulatedSeconds() const noexcept;

    [[nodiscard]] static std::vector<std::pair<uint32_t, uint32_t>> BuildOuterConvexHullEdges(
        const std::vector<Mathematics::Axis3D>& Vertices);

private:
    struct JoltSimulationRuntime;
    std::unique_ptr<JoltSimulationRuntime> Runtime {};
    PhysicsSimulationParameters            Config  {};
};

static_assert(
    Lifecycle::SuspendableParticipant<PhysicsExtension>,
    "PhysicsExtension must satisfy Lifecycle::SuspendableParticipant.");

} // namespace Frontier::SimulationSpace::PhysicsSpace
