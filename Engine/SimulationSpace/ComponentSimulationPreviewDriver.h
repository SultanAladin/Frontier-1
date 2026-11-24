/*============================================================================================================================================
                                                COMPONENTSIMULATIONPREVIEWDRIVER.H
============================================================================================================================================*/
// 🧩 Phase D32 Live Play/Pause/Resume/Stop/Reset 5-domain simulation preview driver bridging any ComponentClassDefinition
//    to SimulationCoordinatorExtension inside Lifecycle::LifecycleSequence with on-demand deformable render surface extraction.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D32-D33)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/ComponentClassSimulationAssembler.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                               PREVIEW DRIVER STATUS
//------------------------------------------------------------------------------------------------------------------------

enum class PreviewRunStatus : uint8_t
{
    Idle    = 0u,
    Running = 1u,
    Paused  = 2u,
};

struct PreviewSurfaceSnapshot
{
    uint32_t                                           BodyInstanceId = 0u;
    std::string                                        Label          {};
    GeometrySpace::Deformation::ProceduralRenderSurface Surface       {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                       COMPONENT SIMULATION PREVIEW DRIVER
//------------------------------------------------------------------------------------------------------------------------

class ComponentSimulationPreviewDriver
{
public:
    ComponentSimulationPreviewDriver() = default;
    ~ComponentSimulationPreviewDriver();

    ComponentSimulationPreviewDriver(const ComponentSimulationPreviewDriver&) = delete;
    ComponentSimulationPreviewDriver& operator=(const ComponentSimulationPreviewDriver&) = delete;

    /// Returns true if `definition` contains at least one SoftBody, ForceField, FluidRegion, or ParticleCloud component.
    [[nodiscard]] static bool QueryHasSimulationComponents(const ComponentClass::ComponentClassDefinition& definition) noexcept;

    /// Starts a live 5-domain simulation preview for `definition`. Returns false if `definition` has no simulation components
    /// or initialization/mounting fails.
    [[nodiscard]] bool StartPreview(
        const ComponentClass::ComponentClassDefinition& definition,
        std::string*                                    outError = nullptr);

    /// Advances the active preview by `deltaSeconds` (no-op unless Running).
    void StepPreview(double deltaSeconds, const std::array<float, 3>& cameraWorldPos = { 4.10f, 2.35f, 4.95f }) noexcept;

    /// Suspends the underlying LifecycleSequence without destroying simulation allocations.
    [[nodiscard]] bool PausePreview() noexcept;

    /// Resumes a paused preview.
    [[nodiscard]] bool ResumePreview() noexcept;

    /// Finalizes the LifecycleSequence and releases preview allocations.
    void StopAndResetPreview() noexcept;

    /// Edit-mode / preview fast-skip toggle (when false, StepPreview returns immediately in 0.00 ms).
    void               SetShouldUpdate(bool shouldUpdate) noexcept;
    [[nodiscard]] bool QueryShouldUpdate() const noexcept { return ShouldUpdate_; }

#if FRONTIER_FLUID_HAS_VULKAN
    /// Attaches a live Vulkan 1.2 interchange for Deformable, Fluid, and Particle async GPU execution.
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* interchange,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    [[nodiscard]] PreviewRunStatus QueryStatus() const noexcept { return RunStatus_; }
    [[nodiscard]] uint64_t         QueryBoundRevision() const noexcept { return BoundRevision_; }
    [[nodiscard]] const PrefabAssemblyReport& InspectAssemblyReport() const noexcept { return AssemblyReport_; }

    /// Inspects latest 5-domain telemetry (returns default-zeroed struct when Idle).
    [[nodiscard]] SimulationCoordinatorTelemetry  InspectTelemetry() const noexcept;
    [[nodiscard]] SimulationExecutionRouteSummary InspectExecutionRoutes() const noexcept;

    /// Builds current world-space deformed triangle surfaces for all active SoftBody instances in the preview.
    [[nodiscard]] std::vector<PreviewSurfaceSnapshot> ExtractDeformableRenderSurfaces() const;

    [[nodiscard]] const SimulationCoordinatorExtension* InspectCoordinator() const noexcept { return Coordinator_.get(); }

private:
    PreviewRunStatus                                RunStatus_      = PreviewRunStatus::Idle;
    bool                                            ShouldUpdate_   = true;
#if FRONTIER_FLUID_HAS_VULKAN
    GeometricRaster::VulkanInterchange*             Interchange_             = nullptr;
    std::filesystem::path                           CompiledShaderDirectory_ = "CompiledShaders";
#endif
    uint64_t                                        BoundRevision_  = 0ull;
    PrefabAssemblyReport                            AssemblyReport_ {};
    std::unique_ptr<SimulationCoordinatorExtension> Coordinator_    {};
    std::unique_ptr<Lifecycle::LifecycleSequence>   Sequence_       {};
};

} // namespace Frontier::SimulationSpace
