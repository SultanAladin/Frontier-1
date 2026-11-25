/*============================================================================================================================================
                                                   WORLDSIMULATIONSESSION.CPP
============================================================================================================================================*/
// 🧩 Phase D34 Implementation of WorldSimulationSession

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace Frontier::SimulationSpace
{

namespace
{

uint64_t PackClassKey(uint32_t slotIndex, uint32_t generation) noexcept
{
    return (static_cast<uint64_t>(generation) << 32u) | static_cast<uint64_t>(slotIndex);
}

} // namespace

WorldSimulationSession::~WorldSimulationSession()
{
    Stop();
}

bool WorldSimulationSession::MountPreparedScene(const SimulationSceneDocument& sceneDoc, std::string* outError)
{
    auto params = SimulationSceneCodec::BuildCoordinatorParameters(sceneDoc);
#if FRONTIER_FLUID_HAS_VULKAN
    if (!CompiledShaderDirectory_.empty())
    {
        params.CompiledShaderDirectory                      = CompiledShaderDirectory_;
        params.DeformableParameters.CompiledShaderDirectory = CompiledShaderDirectory_;
    }
#endif
    Coordinator_ = std::make_unique<SimulationCoordinatorExtension>(std::move(params));
    Coordinator_->SetShouldUpdate(ShouldUpdate_);
#if FRONTIER_FLUID_HAS_VULKAN
    if (Interchange_ != nullptr)
    {
        (void)Coordinator_->AttachVulkanInterchange(Interchange_, CompiledShaderDirectory_);
    }
#endif

    Sequence_ = std::make_unique<Lifecycle::LifecycleSequence>();
    (void)Sequence_->RegisterParticipant("FrontierWorldSimulation", *Coordinator_);
    if (!Sequence_->Initialize())
    {
        if (outError != nullptr) *outError = "Failed to initialize SimulationCoordinatorExtension in WorldSimulationSession.";
        Stop();
        return false;
    }

    if (!SimulationSceneCodec::MountIntoCoordinator(sceneDoc, *Coordinator_, outError))
    {
        Stop();
        return false;
    }

    ActiveScene_ = sceneDoc;
    Profiler_.Reset();
    BudgetGovernor_.Reset();
    EventTimeline_.Rewind(-1.0);
    TriggerRouter_.ClearAll();
    if (Checkpoints_)
    {
        Checkpoints_->PrewarmForCoordinator(*Coordinator_);
    }
    RunStatus_   = PreviewRunStatus::Running;
    return true;
}

bool WorldSimulationSession::BeginPlayFromWorldPlacements(
    std::span<const ComponentClass::ComponentClassDefinition> classDefinitions,
    const ComponentClass::ComponentWorldPlacementRegister&    worldPlacements,
    std::string_view                                          sessionTitle,
    std::string*                                              outError)
{
    Stop();

    std::unordered_map<uint64_t, const ComponentClass::ComponentClassDefinition*> catalog;
    catalog.reserve(classDefinitions.size());
    for (std::size_t classOrdinal = 0u; classOrdinal < classDefinitions.size(); ++classOrdinal)
    {
        catalog.emplace(PackClassKey(static_cast<uint32_t>(classOrdinal + 1u), 1u), &classDefinitions[classOrdinal]);
    }

    AssemblyReport_ = {};
    const SimulationSceneDocument sceneDoc = ComponentClassSimulationAssembler::BuildSceneFromPlacements(
        catalog, worldPlacements, sessionTitle, &AssemblyReport_);

    const uint32_t totalSimEntities = AssemblyReport_.ForcesAssembled + AssemblyReport_.FluidSourcesAssembled +
                                      AssemblyReport_.CloudsAssembled + AssemblyReport_.SoftBodiesAssembled;
    if (totalSimEntities == 0u)
    {
        if (outError != nullptr) *outError = "World placements contain no active simulation components.";
        return false;
    }

    return MountPreparedScene(sceneDoc, outError);
}

bool WorldSimulationSession::BeginPlayFromSceneFile(
    const std::filesystem::path& sceneFilePath,
    std::string*                 outError)
{
    Stop();

    SimulationSceneDocument decoded {};
    if (!SimulationSceneCodec::DecodeFromFile(sceneFilePath, decoded, outError))
    {
        return false;
    }

    AssemblyReport_ = {};
    AssemblyReport_.PlacementsProcessed   = 1u;
    AssemblyReport_.ForcesAssembled       = static_cast<uint32_t>(decoded.Forces.size());
    AssemblyReport_.FluidSourcesAssembled = static_cast<uint32_t>(decoded.FluidSources.size());
    AssemblyReport_.CloudsAssembled       = static_cast<uint32_t>(decoded.ParticleClouds.size());
    AssemblyReport_.SoftBodiesAssembled   = static_cast<uint32_t>(decoded.SoftBodies.size());
    AssemblyReport_.CollidersAssembled    = static_cast<uint32_t>(decoded.RigidColliders.size());

    return MountPreparedScene(decoded, outError);
}

void WorldSimulationSession::Tick(double deltaSeconds, const std::array<float, 3>& cameraWorldPos) noexcept
{
    if (RunStatus_ != PreviewRunStatus::Running || !ShouldUpdate_ || !Coordinator_ || !Sequence_)
    {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    Coordinator_->AssignCameraWorldPosition(cameraWorldPos);
    const double clampedDt = std::clamp(deltaSeconds, 0.0005, 0.05);
    (void)ReplayRecorder_.ApplyPlaybackForTick(Coordinator_->InspectTelemetry().CompletedTicks, *Coordinator_);
    Sequence_->Tick(clampedDt);
    (void)EventTimeline_.EvaluateUpToTime(Coordinator_->InspectTelemetry().AccumulatedSeconds, *Coordinator_);
    if (AtlasFleet_.QueryInstanceCount() > 0u)
    {
        (void)AtlasFleet_.StepFleet(static_cast<float>(clampedDt), cameraWorldPos);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const float elapsedUs = static_cast<float>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) * 0.001f;
    Profiler_.RecordTickMicros(Coordinator_->InspectTelemetry().CompletedTicks, elapsedUs);

    if (BudgetGovernorEnabled_)
    {
        BudgetGovernor_.RecordStepDurationMs(elapsedUs * 0.001f);
        BudgetGovernor_.ApplyToCoordinator(*Coordinator_);
    }
}

bool WorldSimulationSession::StepSingleTick(
    double fixedDeltaSeconds,
    const std::array<float, 3>& cameraWorldPos) noexcept
{
    if (!Coordinator_ || !Sequence_ || RunStatus_ == PreviewRunStatus::Idle)
    {
        return false;
    }

    const bool wasPaused = (RunStatus_ == PreviewRunStatus::Paused);
    if (wasPaused && !Sequence_->Resume())
    {
        return false;
    }

    const bool prevShouldUpdate = ShouldUpdate_;
    Coordinator_->SetShouldUpdate(true);
    Coordinator_->AssignCameraWorldPosition(cameraWorldPos);
    const double clampedDt = std::clamp(fixedDeltaSeconds, 0.0005, 0.05);
    const auto t0 = std::chrono::steady_clock::now();
    (void)ReplayRecorder_.ApplyPlaybackForTick(Coordinator_->InspectTelemetry().CompletedTicks, *Coordinator_);
    Sequence_->Tick(clampedDt);
    (void)EventTimeline_.EvaluateUpToTime(Coordinator_->InspectTelemetry().AccumulatedSeconds, *Coordinator_);
    if (AtlasFleet_.QueryInstanceCount() > 0u)
    {
        (void)AtlasFleet_.StepFleet(static_cast<float>(clampedDt), cameraWorldPos);
    }
    const auto t1 = std::chrono::steady_clock::now();
    Coordinator_->SetShouldUpdate(prevShouldUpdate);

    const float elapsedUs = static_cast<float>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) * 0.001f;
    Profiler_.RecordTickMicros(Coordinator_->InspectTelemetry().CompletedTicks, elapsedUs);

    if (wasPaused)
    {
        (void)Sequence_->Suspend();
    }
    return true;
}

void WorldSimulationSession::EnableBudgetGovernor(const SimulationBudgetPolicy& policy) noexcept
{
    BudgetGovernor_        = SimulationBudgetGovernor(policy);
    BudgetGovernorEnabled_ = true;
    Profiler_.SetTargetBudgetMs(policy.TargetSimBudgetMs);
}

void WorldSimulationSession::EnableCheckpointRing(uint32_t ringCapacity)
{
    Checkpoints_ = std::make_unique<SimulationCheckpointBuffer>(ringCapacity);
    if (Coordinator_)
    {
        Checkpoints_->PrewarmForCoordinator(*Coordinator_);
    }
}

uint64_t WorldSimulationSession::CaptureCheckpoint()
{
    if (!Coordinator_) return 0ull;
    if (!Checkpoints_)
    {
        EnableCheckpointRing(32u);
    }
    return Checkpoints_->Capture(*Coordinator_);
}

bool WorldSimulationSession::RewindToCheckpoint(uint32_t chronologicalOrdinal)
{
    if (!Coordinator_ || !Checkpoints_) return false;
    if (!Checkpoints_->RestoreByOrdinal(chronologicalOrdinal, *Coordinator_))
    {
        return false;
    }
    EventTimeline_.Rewind(Coordinator_->InspectTelemetry().AccumulatedSeconds);
    return true;
}

uint32_t WorldSimulationSession::EvaluateTriggers(const TriggerDomainPool& triggerPool) noexcept
{
    if (!Coordinator_) return 0u;
    return TriggerRouter_.EvaluateAndDispatch(triggerPool, *Coordinator_);
}

SoftBodyGrabHandle WorldSimulationSession::PickSoftBodyPoint(
    const std::array<float, 3>& rayOrigin,
    const std::array<float, 3>& rayDir,
    float maxDistM) const noexcept
{
    if (!Coordinator_) return {};
    return SimulationInteractionBridge::PickSoftBodyPoint(*Coordinator_, rayOrigin, rayDir, maxDistM);
}

bool WorldSimulationSession::BeginSoftBodyGrab(const SoftBodyGrabHandle& handle) noexcept
{
    if (!Coordinator_) return false;
    const bool ok = SimulationInteractionBridge::BeginSoftBodyGrab(*Coordinator_, handle);
    if (ok && ReplayRecorder_.IsRecording())
    {
        (void)ReplayRecorder_.RecordSoftBodyGrabBegin(Coordinator_->InspectTelemetry().CompletedTicks, handle);
    }
    return ok;
}

bool WorldSimulationSession::MoveSoftBodyGrab(
    const SoftBodyGrabHandle& handle,
    const std::array<float, 3>& targetMeters) noexcept
{
    if (!Coordinator_) return false;
    const bool ok = SimulationInteractionBridge::MoveSoftBodyGrab(*Coordinator_, handle, targetMeters);
    if (ok && ReplayRecorder_.IsRecording())
    {
        (void)ReplayRecorder_.RecordSoftBodyGrabMove(Coordinator_->InspectTelemetry().CompletedTicks, handle, targetMeters);
    }
    return ok;
}

void WorldSimulationSession::ReleaseSoftBodyGrab(const SoftBodyGrabHandle& handle) noexcept
{
    if (!Coordinator_) return;
    SimulationInteractionBridge::ReleaseSoftBodyGrab(*Coordinator_, handle);
    if (ReplayRecorder_.IsRecording())
    {
        (void)ReplayRecorder_.RecordSoftBodyGrabRelease(Coordinator_->InspectTelemetry().CompletedTicks, handle);
    }
}

uint32_t WorldSimulationSession::InjectFluidBrushStroke(
    const std::array<float, 3>& centreMeters,
    const std::array<float, 3>& velocityMps,
    float radiusMeters,
    float density,
    float temp) noexcept
{
    if (!Coordinator_) return 0u;
    if (ReplayRecorder_.IsRecording())
    {
        (void)ReplayRecorder_.RecordFluidBrushStroke(
            Coordinator_->InspectTelemetry().CompletedTicks, centreMeters, velocityMps, density, temp, radiusMeters);
    }
    return SimulationInteractionBridge::InjectFluidBrushStroke(*Coordinator_, centreMeters, velocityMps, density, temp, radiusMeters);
}

RadialBlastReport WorldSimulationSession::InjectRadialBlastImpulse(
    const std::array<float, 3>& centreMeters,
    float radiusMeters,
    float peakSpeedMps,
    float fluidTempBoost) noexcept
{
    if (!Coordinator_) return {};
    if (ReplayRecorder_.IsRecording())
    {
        (void)ReplayRecorder_.RecordRadialBlast(
            Coordinator_->InspectTelemetry().CompletedTicks, centreMeters, radiusMeters, peakSpeedMps, fluidTempBoost);
    }
    AcousticBridge_.NotifyRadialBlast(centreMeters, peakSpeedMps);
    return SimulationInteractionBridge::InjectRadialBlastImpulse(*Coordinator_, centreMeters, radiusMeters, peakSpeedMps, fluidTempBoost);
}

SimulationAcousticPacket WorldSimulationSession::ExtractAcousticPacket(
    const std::array<float, 3>& listenerWorldMeters) noexcept
{
    if (!Coordinator_) return {};
    return AcousticBridge_.ExtractPacket(*Coordinator_, listenerWorldMeters);
}

AeroBuoyancyStepReport WorldSimulationSession::ApplyAeroBuoyancy(
    PhysicsDomainPool& physicsPool,
    float deltaTimeSeconds,
    const AeroBuoyancyParameters& params) const noexcept
{
    if (!Coordinator_) return {};
    return SimulationAeroBuoyancyBridge::StepPool(*Coordinator_, physicsPool, deltaTimeSeconds, params);
}

FractureCouplingReport WorldSimulationSession::TriggerFractureImpact(
    const FractureImpactEvent& impact,
    PhysicsDomainPool* shardPool) noexcept
{
    if (!Coordinator_) return {};
    return SimulationFractureCouplingBridge::TriggerFractureImpact(*Coordinator_, impact, shardPool, &AcousticBridge_);
}

SimulationPointProbe WorldSimulationSession::SampleWorldPoint(
    const std::array<float, 3>& worldPosMeters,
    const std::array<float, 3>& probeVelocityMps) const noexcept
{
    if (!Coordinator_) return {};
    return SimulationSpatialQuery::SampleWorldPoint(*Coordinator_, worldPosMeters, probeVelocityMps);
}

SimulationSphereSummary WorldSimulationSession::QuerySphereInfluence(
    const std::array<float, 3>& centreMeters,
    float radiusMeters) const noexcept
{
    if (!Coordinator_) return {};
    return SimulationSpatialQuery::QuerySphereInfluence(*Coordinator_, centreMeters, radiusMeters);
}

SimulationBoundsSnapshot WorldSimulationSession::ComputeDomainWorldBounds() const noexcept
{
    if (!Coordinator_) return {};
    return SimulationSpatialQuery::ComputeDomainWorldBounds(*Coordinator_);
}

void WorldSimulationSession::SetShouldUpdate(bool shouldUpdate) noexcept
{
    ShouldUpdate_ = shouldUpdate;
    if (Coordinator_)
    {
        Coordinator_->SetShouldUpdate(shouldUpdate);
    }
}

#if FRONTIER_FLUID_HAS_VULKAN
bool WorldSimulationSession::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* interchange,
    const std::filesystem::path& compiledShaderDirectory) noexcept
{
    Interchange_ = interchange;
    if (!compiledShaderDirectory.empty())
    {
        CompiledShaderDirectory_ = compiledShaderDirectory;
    }
    return Coordinator_
        ? Coordinator_->AttachVulkanInterchange(interchange, CompiledShaderDirectory_)
        : (interchange != nullptr && interchange->Ready());
}
#endif

bool WorldSimulationSession::Pause() noexcept
{
    if (RunStatus_ != PreviewRunStatus::Running || !Sequence_)
    {
        return false;
    }
    if (!Sequence_->Suspend())
    {
        return false;
    }
    RunStatus_ = PreviewRunStatus::Paused;
    return true;
}

bool WorldSimulationSession::Resume() noexcept
{
    if (RunStatus_ != PreviewRunStatus::Paused || !Sequence_)
    {
        return false;
    }
    if (!Sequence_->Resume())
    {
        return false;
    }
    RunStatus_ = PreviewRunStatus::Running;
    return true;
}

void WorldSimulationSession::Stop() noexcept
{
    if (Sequence_)
    {
        if (Coordinator_)
        {
            Sequence_->Finalize();
        }
        Sequence_.reset();
    }
    Coordinator_.reset();
    AssemblyReport_ = {};
    ActiveScene_    = {};
    RunStatus_      = PreviewRunStatus::Idle;
}

SimulationCoordinatorTelemetry WorldSimulationSession::InspectTelemetry() const noexcept
{
    if (!Coordinator_)
    {
        return {};
    }
    return Coordinator_->InspectTelemetry();
}

SimulationExecutionRouteSummary WorldSimulationSession::InspectExecutionRoutes() const noexcept
{
    if (!Coordinator_)
    {
        return {};
    }
    return Coordinator_->InspectExecutionRoutes();
}

void WorldSimulationSession::PopulateGpuRenderFeed(SimulationGpuRenderFeedPacket& outPacket) const noexcept
{
    if (!Coordinator_)
    {
        outPacket = {};
        return;
    }
    PopulateSimulationGpuRenderFeed(*Coordinator_, outPacket);
}

std::vector<PreviewSurfaceSnapshot> WorldSimulationSession::ExtractDeformableRenderSurfaces() const
{
    std::vector<PreviewSurfaceSnapshot> out;
    if (!Coordinator_)
    {
        return out;
    }

    const auto& worldReg = Coordinator_->InspectDeformables().InspectWorldRegistry();
    for (const auto& entry : worldReg.EnumerateInstances())
    {
        if (!entry.Enabled) continue;
        PreviewSurfaceSnapshot snap {};
        snap.BodyInstanceId = entry.InstanceId;
        snap.Label          = entry.InstanceName;
        GeometrySpace::Deformation::DeformableSurfaceBuilder::AppendBodyAttachments(entry.Body, snap.Surface, true, true);
        out.push_back(std::move(snap));
    }
    return out;
}

std::string WorldSimulationSession::ExportActiveSceneToToml() const
{
    return SimulationSceneCodec::EncodeToTomlText(ActiveScene_);
}

} // namespace Frontier::SimulationSpace
