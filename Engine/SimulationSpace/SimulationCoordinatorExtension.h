/*============================================================================================================================================
                                                SIMULATIONCOORDINATOREXTENSION.H
============================================================================================================================================*/
// 🧩 Phase D22 Unified Frontier Lifecycle::SuspendableParticipant orchestrating all four simulation engines
//    in deterministic upstream dependency order: ForceFields -> Fluid -> ParticleSpace -> Deformables (+ Jolt proxy feedback).
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D22)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/ParticleSpace/CarrierSolidRegistry.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SimulationSpace/Deformable/DeformableExtension.h"
#include "Engine/SimulationSpace/Deformable/VehicleReadingConverter.h"
#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                               COORDINATOR CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct SimulationCoordinatorParameters
{
    float                                    FixedStepHz             = 60.0f;
    uint32_t                                 MaxSubstepsPerTick      = 4u;
    bool                                     EnableFluidSolver       = true;
    bool                                     EnableParticleSolver    = true;
    bool                                     EnableDeformableSolver  = true;
    std::array<uint32_t, 3>                  FluidGridCells          { 16u, 16u, 16u };
    float                                    FluidCellSizeMeters     = 0.28f;
    std::array<float, 3>                     FluidCentreMeters       { 0.0f, 1.8f, 0.0f };
    Fluid::FluidStepSettings                 FluidSettings           {};
    Deformable::DeformableExtensionParameters DeformableParameters   {};
    float                                    DeformableImpulseGain   = 18.0f;
    std::filesystem::path                    CompiledShaderDirectory = "CompiledShaders";
};

struct SimulationCoordinatorTelemetry
{
    uint64_t                                  CompletedTicks           = 0ull;
    double                                    AccumulatedSeconds       = 0.0;
    uint32_t                                  ActiveForceCount         = 0u;
    uint64_t                                  FluidStepCount           = 0ull;
    float                                     FluidPeakDensity         = 0.0f;
    uint32_t                                  ActiveParticleCount      = 0u;
    uint32_t                                  ActiveDeformableCount    = 0u;
    uint32_t                                  DeformableRigidContacts  = 0u;
    Deformable::VehicleCouplingSummary        LastVehicleSummary       {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SIMULATION COORDINATOR EXTENSION
//------------------------------------------------------------------------------------------------------------------------

/// Deterministic Frontier engine runtime coordinator coupling ForceFields, 3D Eulerian Fluid,
/// GPU ParticleSpace, Multi-Instance Deformables, 4-Wheel Vehicle Tires, and Jolt RigidColliderProxy feedback.
struct SimulationExecutionRouteSummary
{
    Deformable::DeformableExecutionRoute DeformableRoute      = Deformable::DeformableExecutionRoute::HeadlessCpuMirror;
    Fluid::FluidExecutionRoute           FluidRoute           = Fluid::FluidExecutionRoute::HeadlessCpuMirror;
    ParticleSpace::ParticleExecutionRoute ParticleRoute       = ParticleSpace::ParticleExecutionRoute::HeadlessCpuMirror;
    bool                                 AnyRealGpuDispatched = false;
    bool                                 AllRealGpuDispatched = false;
};

class SimulationCoordinatorExtension
{
public:
    SimulationCoordinatorExtension() noexcept = default;
    explicit SimulationCoordinatorExtension(SimulationCoordinatorParameters initialParameters) noexcept;
    ~SimulationCoordinatorExtension() noexcept;

    SimulationCoordinatorExtension(const SimulationCoordinatorExtension&)            = delete;
    SimulationCoordinatorExtension& operator=(const SimulationCoordinatorExtension&) = delete;
    SimulationCoordinatorExtension(SimulationCoordinatorExtension&&)                 = delete;
    SimulationCoordinatorExtension& operator=(SimulationCoordinatorExtension&&)      = delete;

    // Frontier::Lifecycle::SuspendableParticipant contract
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double deltaSeconds) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool QueryInitialized() const noexcept { return Initialized_; }
    [[nodiscard]] bool QuerySuspended() const noexcept { return Suspended_; }
    void               SetShouldUpdate(bool shouldUpdate) noexcept
    {
        ShouldUpdate_ = shouldUpdate;
        Deformables_.SetShouldUpdate(shouldUpdate);
    }
    [[nodiscard]] bool QueryShouldUpdate() const noexcept { return ShouldUpdate_; }

#if FRONTIER_FLUID_HAS_VULKAN
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* interchange,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    /// Honest consolidated execution route summary across Deformable, Fluid, and Particle GPU executors.
    [[nodiscard]] SimulationExecutionRouteSummary InspectExecutionRoutes() const noexcept;

    // Cross-domain scene authoring & external state sync
    void AssignCameraWorldPosition(const std::array<float, 3>& cameraPositionMeters) noexcept;
    void AssignRigidColliderProxies(std::span<const PhysicsSpace::RigidColliderProxy> proxies);
    void SyncVehicleFromPhysicsReading(
        const PhysicsSpace::VehicleReading& vehicleReading,
        const Deformable::VehicleConversionSettings& conversion = {}) noexcept;

    void AddFluidSource(const Fluid::FluidReleaseSourceParameters& source);
    void ClearFluidSources() noexcept;
    void AddParticleCloud(const ParticleSpace::ParticleCloudNode& cloud);
    void ClearParticleClouds() noexcept;

    // Trilinear world-space velocity query into the live 3D Fluid grid (used by ParticleSpace & diagnostics)
    [[nodiscard]] std::array<float, 3> SampleFluidVelocityWorld(const std::array<float, 3>& worldPosition) const noexcept;

    // Subsystem accessors
    [[nodiscard]] ForceFields::ForceFieldRegister&           AccessForceFields() noexcept { return ForceRegister_; }
    [[nodiscard]] const ForceFields::ForceFieldRegister&     InspectForceFields() const noexcept { return ForceRegister_; }
    [[nodiscard]] Fluid::FluidCpuSimulationState&            AccessFluidSimulation() noexcept { return FluidSim_; }
    [[nodiscard]] const Fluid::FluidCpuSimulationState&      InspectFluidSimulation() const noexcept { return FluidSim_; }
    [[nodiscard]] ParticleSpace::ParticleGpuSimulation&      AccessParticles() noexcept { return ParticleSim_; }
    [[nodiscard]] const ParticleSpace::ParticleGpuSimulation& InspectParticles() const noexcept { return ParticleSim_; }
    [[nodiscard]] Deformable::DeformableExtension&           AccessDeformables() noexcept { return Deformables_; }
    [[nodiscard]] const Deformable::DeformableExtension&     InspectDeformables() const noexcept { return Deformables_; }

    [[nodiscard]] Fluid::FluidVulkanGpuExecutor&                   AccessFluidGpuExecutor() noexcept { return FluidGpuExecutor_; }
    [[nodiscard]] const Fluid::FluidVulkanGpuExecutor&             InspectFluidGpuExecutor() const noexcept { return FluidGpuExecutor_; }
    [[nodiscard]] ParticleSpace::ParticleVulkanGpuExecutor&        AccessParticleGpuExecutor() noexcept { return ParticleGpuExecutor_; }
    [[nodiscard]] const ParticleSpace::ParticleVulkanGpuExecutor&  InspectParticleGpuExecutor() const noexcept { return ParticleGpuExecutor_; }

    [[nodiscard]] std::span<const PhysicsSpace::RigidImpulseAccumulatorEntry> InspectRigidImpulseFeedback() const noexcept
    {
        return RigidFeedback_;
    }

    [[nodiscard]] const SimulationCoordinatorParameters& InspectParameters() const noexcept { return Parameters_; }
    [[nodiscard]] SimulationCoordinatorParameters&       AccessParameters() noexcept { return Parameters_; }
    [[nodiscard]] const SimulationCoordinatorTelemetry&  InspectTelemetry() const noexcept { return Telemetry_; }
    void RestoreClockState(uint64_t completedTicks, double accumulatedSeconds) noexcept
    {
        Telemetry_.CompletedTicks     = completedTicks;
        Telemetry_.AccumulatedSeconds = accumulatedSeconds;
        StepAccumulator_              = 0.0;
        Deformables_.RestoreClockState(completedTicks, accumulatedSeconds, completedTicks);
    }

private:
    static std::array<float, 3> SampleFluidVelocityThunk(const void* userContext, const std::array<float, 3>& worldPosition) noexcept;

    SimulationCoordinatorParameters                           Parameters_          {};
    ForceFields::ForceFieldRegister                           ForceRegister_       {};
    Fluid::FluidCpuSimulationState                            FluidSim_            {};
    Fluid::FluidVulkanGpuExecutor                             FluidGpuExecutor_    {};
    std::vector<Fluid::FluidReleaseSourceParameters>          FluidSources_        {};
    ParticleSpace::CarrierSolidRegistry                       SolidRegistry_       {};
    ParticleSpace::ParticleGpuSimulation                      ParticleSim_         {};
    ParticleSpace::ParticleVulkanGpuExecutor                  ParticleGpuExecutor_ {};
    std::vector<ParticleSpace::ParticleCloudNode>             ParticleClouds_      {};
    Deformable::DeformableExtension                           Deformables_         {};
    std::vector<PhysicsSpace::RigidColliderProxy>             RigidProxies_        {};
    std::vector<Fluid::FluidColliderSphere>                   FluidColliders_      {};
    std::vector<PhysicsSpace::RigidImpulseAccumulatorEntry>   RigidFeedback_       {};
    std::array<float, 3>                                      CameraWorldPos_      { 0.0f, 2.0f, 6.0f };
    double                                                    StepAccumulator_     = 0.0;
    SimulationCoordinatorTelemetry                            Telemetry_           {};
    bool                                                      Initialized_         = false;
    bool                                                      Suspended_           = false;
    bool                                                      ShouldUpdate_        = true;
};

static_assert(Lifecycle::SuspendableParticipant<SimulationCoordinatorExtension>,
    "SimulationCoordinatorExtension must satisfy Frontier::Lifecycle::SuspendableParticipant");

} // namespace Frontier::SimulationSpace
