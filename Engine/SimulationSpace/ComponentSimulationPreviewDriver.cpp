/*============================================================================================================================================
                                               COMPONENTSIMULATIONPREVIEWDRIVER.CPP
============================================================================================================================================*/
// 🧩 Phase D32 Implementation of ComponentSimulationPreviewDriver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ComponentSimulationPreviewDriver.h"

#include <algorithm>
#include <unordered_map>

namespace Frontier::SimulationSpace
{

ComponentSimulationPreviewDriver::~ComponentSimulationPreviewDriver()
{
    StopAndResetPreview();
}

bool ComponentSimulationPreviewDriver::QueryHasSimulationComponents(
    const ComponentClass::ComponentClassDefinition& definition) noexcept
{
    const auto metrics = definition.Storage.ComputeSummaryMetrics();
    return (metrics.SoftBodyCount + metrics.ForceFieldCount + metrics.FluidRegionCount + metrics.ParticleCloudCount) > 0u;
}

bool ComponentSimulationPreviewDriver::StartPreview(
    const ComponentClass::ComponentClassDefinition& definition,
    std::string*                                    outError)
{
    StopAndResetPreview();

    if (!QueryHasSimulationComponents(definition))
    {
        if (outError != nullptr) *outError = "ComponentClassDefinition has no simulation components to preview.";
        return false;
    }

    ComponentClass::ComponentWorldPlacementRegister placementReg {};
    const ComponentClass::ComponentClassToken classTok { 1u, 1u };
    (void)placementReg.SpawnPlacement(
        classTok,
        definition.Title.empty() ? "PreviewRig" : definition.Title,
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    std::unordered_map<uint64_t, const ComponentClass::ComponentClassDefinition*> catalog;
    const uint64_t packedKey = (static_cast<uint64_t>(classTok.Generation) << 32u) | static_cast<uint64_t>(classTok.SlotIndex);
    catalog.emplace(packedKey, &definition);

    AssemblyReport_ = {};
    const SimulationSceneDocument sceneDoc = ComponentClassSimulationAssembler::BuildSceneFromPlacements(
        catalog, placementReg, definition.Title, &AssemblyReport_);

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
    (void)Sequence_->RegisterParticipant("ComponentSimPreview", *Coordinator_);
    if (!Sequence_->Initialize())
    {
        if (outError != nullptr) *outError = "Failed to initialize SimulationCoordinatorExtension for preview.";
        StopAndResetPreview();
        return false;
    }

    if (!SimulationSceneCodec::MountIntoCoordinator(sceneDoc, *Coordinator_, outError))
    {
        StopAndResetPreview();
        return false;
    }

    BoundRevision_ = definition.RevisionNumber;
    RunStatus_     = PreviewRunStatus::Running;
    return true;
}

void ComponentSimulationPreviewDriver::StepPreview(
    double deltaSeconds,
    const std::array<float, 3>& cameraWorldPos) noexcept
{
    if (RunStatus_ != PreviewRunStatus::Running || !ShouldUpdate_ || !Coordinator_ || !Sequence_)
    {
        return;
    }
    Coordinator_->AssignCameraWorldPosition(cameraWorldPos);
    const double clampedDt = std::clamp(deltaSeconds, 0.0005, 0.05);
    Sequence_->Tick(clampedDt);
}

void ComponentSimulationPreviewDriver::SetShouldUpdate(bool shouldUpdate) noexcept
{
    ShouldUpdate_ = shouldUpdate;
    if (Coordinator_)
    {
        Coordinator_->SetShouldUpdate(shouldUpdate);
    }
}

#if FRONTIER_FLUID_HAS_VULKAN
bool ComponentSimulationPreviewDriver::AttachVulkanInterchange(
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

bool ComponentSimulationPreviewDriver::PausePreview() noexcept
{
    if (RunStatus_ != PreviewRunStatus::Running || !Coordinator_ || !Sequence_)
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

bool ComponentSimulationPreviewDriver::ResumePreview() noexcept
{
    if (RunStatus_ != PreviewRunStatus::Paused || !Coordinator_ || !Sequence_)
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

void ComponentSimulationPreviewDriver::StopAndResetPreview() noexcept
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
    BoundRevision_  = 0ull;
    RunStatus_      = PreviewRunStatus::Idle;
}

SimulationCoordinatorTelemetry ComponentSimulationPreviewDriver::InspectTelemetry() const noexcept
{
    if (!Coordinator_)
    {
        return {};
    }
    return Coordinator_->InspectTelemetry();
}

SimulationExecutionRouteSummary ComponentSimulationPreviewDriver::InspectExecutionRoutes() const noexcept
{
    if (!Coordinator_)
    {
        return {};
    }
    return Coordinator_->InspectExecutionRoutes();
}

std::vector<PreviewSurfaceSnapshot> ComponentSimulationPreviewDriver::ExtractDeformableRenderSurfaces() const
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

} // namespace Frontier::SimulationSpace
