/*============================================================================================================================================
                                                    WORLDSIMULATIONSESSION.H
============================================================================================================================================*/
// 🧩 Phase D34 Unified Play-in-Editor & FrontierWorld 5-domain runtime session launching live coupled simulation directly
//    from EditorLifecycle ComponentDefinitions + WorldPlacements OR from a .VolumetricSpace scene file on disk.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D34-D35)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentWorldPlacement.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/ComponentClassSimulationAssembler.h"
#include "Engine/SimulationSpace/ComponentSimulationPreviewDriver.h"
#include "Engine/SimulationSpace/Deformable/VertexAtlasFleetTable.h"
#include "Engine/SimulationSpace/SimulationAcousticBridge.h"
#include "Engine/SimulationSpace/SimulationAeroBuoyancyBridge.h"
#include "Engine/SimulationSpace/SimulationBudgetGovernor.h"
#include "Engine/SimulationSpace/SimulationCheckpointBuffer.h"
#include "Engine/SimulationSpace/SimulationEventTimeline.h"
#include "Engine/SimulationSpace/SimulationFractureCouplingBridge.h"
#include "Engine/SimulationSpace/SimulationGpuRenderFeed.h"
#include "Engine/SimulationSpace/SimulationInteractionBridge.h"
#include "Engine/SimulationSpace/SimulationReplayRecorder.h"
#include "Engine/SimulationSpace/SimulationSpatialQuery.h"
#include "Engine/SimulationSpace/SimulationTelemetryProfiler.h"
#include "Engine/SimulationSpace/SimulationTriggerRouter.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                WORLD SIMULATION SESSION
//------------------------------------------------------------------------------------------------------------------------

class WorldSimulationSession
{
public:
    WorldSimulationSession() = default;
    ~WorldSimulationSession();

    WorldSimulationSession(const WorldSimulationSession&) = delete;
    WorldSimulationSession& operator=(const WorldSimulationSession&) = delete;

    /// Launches a live 5-domain coupled world simulation from EditorLifecycle's ComponentDefinitions (1-based SlotIndex
    /// matching ComponentClassToken{ classOrdinal + 1u, 1u }) and WorldPlacements (including per-instance overrides).
    [[nodiscard]] bool BeginPlayFromWorldPlacements(
        std::span<const ComponentClass::ComponentClassDefinition> classDefinitions,
        const ComponentClass::ComponentWorldPlacementRegister&    worldPlacements,
        std::string_view                                          sessionTitle = "FrontierWorldPlay",
        std::string*                                              outError = nullptr);

    /// Launches a live 5-domain coupled world simulation directly from a pure-TOML .VolumetricSpace scene file.
    [[nodiscard]] bool BeginPlayFromSceneFile(
        const std::filesystem::path& sceneFilePath,
        std::string*                 outError = nullptr);

    /// Advances the active world simulation by `deltaSeconds` with the viewer's live world camera position.
    void Tick(double deltaSeconds, const std::array<float, 3>& cameraWorldPos = { 4.10f, 2.35f, 4.95f }) noexcept;

    /// Deterministic single-step advance usable even while Paused (for frame-by-frame scrubbing & debugging).
    [[nodiscard]] bool StepSingleTick(
        double fixedDeltaSeconds = 1.0 / 60.0,
        const std::array<float, 3>& cameraWorldPos = { 4.10f, 2.35f, 4.95f }) noexcept;

    [[nodiscard]] bool Pause() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               Stop() noexcept;

    /// Enables or disables automatic adaptive budget governing (248 FPS / 120 FPS / 60 FPS quality tier protection).
    void EnableBudgetGovernor(const SimulationBudgetPolicy& policy = {}) noexcept;
    void DisableBudgetGovernor() noexcept { BudgetGovernorEnabled_ = false; }
    [[nodiscard]] bool QueryBudgetGovernorEnabled() const noexcept { return BudgetGovernorEnabled_; }
    [[nodiscard]] const SimulationBudgetTelemetry& InspectBudgetTelemetry() const noexcept
    {
        return BudgetGovernor_.InspectTelemetry();
    }

    /// Enables and pre-warms the deterministic checkpoint ring for zero-allocation capture & instant rewind.
    void EnableCheckpointRing(uint32_t ringCapacity = 32u);
    [[nodiscard]] uint64_t CaptureCheckpoint();
    [[nodiscard]] bool     RewindToCheckpoint(uint32_t chronologicalOrdinal);
    [[nodiscard]] uint32_t QueryCheckpointCount() const noexcept
    {
        return Checkpoints_ ? Checkpoints_->QueryCount() : 0u;
    }

    /// Zero-allocation wall-clock timing percentiles (p50 / p95 / p99 / peak microseconds) across recent ticks.
    [[nodiscard]] SimulationTimingPercentiles InspectTimingPercentiles() const noexcept
    {
        return Profiler_.ComputePercentiles();
    }
    [[nodiscard]] const SimulationTelemetryProfiler& InspectProfiler() const noexcept { return Profiler_; }

    /// Deterministic multi-domain event timeline (evaluated automatically during Tick / StepSingleTick and rewound on RewindToCheckpoint).
    [[nodiscard]] SimulationEventTimeline&       AccessEventTimeline() noexcept { return EventTimeline_; }
    [[nodiscard]] const SimulationEventTimeline& InspectEventTimeline() const noexcept { return EventTimeline_; }

    /// Trigger-zone router evaluated explicitly against a live TriggerDomainPool in zero allocations.
    [[nodiscard]] SimulationTriggerRouter&       AccessTriggerRouter() noexcept { return TriggerRouter_; }
    [[nodiscard]] const SimulationTriggerRouter& InspectTriggerRouter() const noexcept { return TriggerRouter_; }
    [[nodiscard]] uint32_t                       EvaluateTriggers(const TriggerDomainPool& triggerPool) noexcept;

    /// Deterministic interactive input recorder / replayer (0 heap allocations during record & playback).
    [[nodiscard]] SimulationReplayRecorder&       AccessReplayRecorder() noexcept { return ReplayRecorder_; }
    [[nodiscard]] const SimulationReplayRecorder& InspectReplayRecorder() const noexcept { return ReplayRecorder_; }

    /// Extracts zero-allocation coupled AudioSpace & haptic cues relative to `listenerWorldMeters`.
    [[nodiscard]] SimulationAcousticPacket ExtractAcousticPacket(const std::array<float, 3>& listenerWorldMeters) noexcept;

    /// Integrates Fluid aerodynamic drag + thermal buoyancy lift + ForceFields onto `physicsPool` (0 heap allocations).
    [[nodiscard]] AeroBuoyancyStepReport ApplyAeroBuoyancy(
        PhysicsDomainPool& physicsPool,
        float deltaTimeSeconds,
        const AeroBuoyancyParameters& params = {}) const noexcept;

    /// Multi-clip .VertexAtlas fleet instancing table (stepped automatically during Tick / StepSingleTick in 0 allocs).
    [[nodiscard]] Deformable::VertexAtlasFleetTable&       AccessAtlasFleet() noexcept { return AtlasFleet_; }
    [[nodiscard]] const Deformable::VertexAtlasFleetTable& InspectAtlasFleet() const noexcept { return AtlasFleet_; }

    /// Triggers coupled destructible fracture impact across Fluid dust, SoftBodies, Physics shards & Acoustics (0 allocs).
    [[nodiscard]] FractureCouplingReport TriggerFractureImpact(
        const FractureImpactEvent& impact,
        PhysicsDomainPool* shardPool = nullptr) noexcept;

    /// Zero-allocation interactive manipulation & multi-domain spatial probe helpers.
    [[nodiscard]] SoftBodyGrabHandle       PickSoftBodyPoint(const std::array<float, 3>& rayOrigin, const std::array<float, 3>& rayDir, float maxDistM = 0.25f) const noexcept;
    [[nodiscard]] bool                     BeginSoftBodyGrab(const SoftBodyGrabHandle& handle) noexcept;
    [[nodiscard]] bool                     MoveSoftBodyGrab(const SoftBodyGrabHandle& handle, const std::array<float, 3>& targetMeters) noexcept;
    void                                   ReleaseSoftBodyGrab(const SoftBodyGrabHandle& handle) noexcept;
    [[nodiscard]] uint32_t                 InjectFluidBrushStroke(const std::array<float, 3>& centreMeters, const std::array<float, 3>& velocityMps, float radiusMeters, float density = 1.5f, float temp = 0.5f) noexcept;
    [[nodiscard]] RadialBlastReport        InjectRadialBlastImpulse(const std::array<float, 3>& centreMeters, float radiusMeters, float peakSpeedMps, float fluidTempBoost = 1.2f) noexcept;
    [[nodiscard]] SimulationPointProbe     SampleWorldPoint(const std::array<float, 3>& worldPosMeters, const std::array<float, 3>& probeVelocityMps = { 0.0f, 0.0f, 0.0f }) const noexcept;
    [[nodiscard]] SimulationSphereSummary  QuerySphereInfluence(const std::array<float, 3>& centreMeters, float radiusMeters) const noexcept;
    [[nodiscard]] SimulationBoundsSnapshot ComputeDomainWorldBounds() const noexcept;

    /// Edit-mode / runtime fast-skip toggle (when false, Tick() returns immediately in 0.00 ms).
    void               SetShouldUpdate(bool shouldUpdate) noexcept;
    [[nodiscard]] bool QueryShouldUpdate() const noexcept { return ShouldUpdate_; }

#if FRONTIER_FLUID_HAS_VULKAN
    /// Attaches a live Vulkan 1.2 interchange for Deformable, Fluid, and Particle async GPU execution.
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* interchange,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    [[nodiscard]] PreviewRunStatus QueryStatus() const noexcept { return RunStatus_; }
    [[nodiscard]] bool             QueryPlaying() const noexcept { return RunStatus_ == PreviewRunStatus::Running; }
    [[nodiscard]] const PrefabAssemblyReport&    InspectAssemblyReport() const noexcept { return AssemblyReport_; }
    [[nodiscard]] const SimulationSceneDocument& InspectActiveScene() const noexcept { return ActiveScene_; }
    [[nodiscard]] SimulationCoordinatorTelemetry  InspectTelemetry() const noexcept;
    [[nodiscard]] SimulationExecutionRouteSummary InspectExecutionRoutes() const noexcept;

    /// Populates a fixed-capacity GPU render-feed packet in 0 heap allocations for direct World Renderer consumption.
    void PopulateGpuRenderFeed(SimulationGpuRenderFeedPacket& outPacket) const noexcept;

    /// Extracts current deformed world-space triangle surfaces for all active SoftBody instances.
    [[nodiscard]] std::vector<PreviewSurfaceSnapshot> ExtractDeformableRenderSurfaces() const;

    /// Serializes the active world scene document to pure-TOML text.
    [[nodiscard]] std::string ExportActiveSceneToToml() const;

    [[nodiscard]] const SimulationCoordinatorExtension* InspectCoordinator() const noexcept { return Coordinator_.get(); }

private:
    [[nodiscard]] bool MountPreparedScene(const SimulationSceneDocument& sceneDoc, std::string* outError);

    PreviewRunStatus                                RunStatus_      = PreviewRunStatus::Idle;
    bool                                            ShouldUpdate_   = true;
#if FRONTIER_FLUID_HAS_VULKAN
    GeometricRaster::VulkanInterchange*             Interchange_             = nullptr;
    std::filesystem::path                           CompiledShaderDirectory_ = "CompiledShaders";
#endif
    PrefabAssemblyReport                            AssemblyReport_ {};
    SimulationSceneDocument                         ActiveScene_    {};
    std::unique_ptr<SimulationCoordinatorExtension> Coordinator_    {};
    std::unique_ptr<Lifecycle::LifecycleSequence>   Sequence_       {};
    SimulationBudgetGovernor                        BudgetGovernor_ {};
    bool                                            BudgetGovernorEnabled_ = false;
    std::unique_ptr<SimulationCheckpointBuffer>     Checkpoints_    {};
    SimulationTelemetryProfiler                     Profiler_       {};
    SimulationEventTimeline                         EventTimeline_  {};
    SimulationTriggerRouter                         TriggerRouter_  {};
    SimulationReplayRecorder                        ReplayRecorder_ {};
    SimulationAcousticBridge                        AcousticBridge_ {};
    Deformable::VertexAtlasFleetTable               AtlasFleet_     {};
};

} // namespace Frontier::SimulationSpace
