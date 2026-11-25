/*============================================================================================================================================
                                                        DEFORMABLEEXTENSION.H
============================================================================================================================================*/
// 🧩 Phase D19 Official Frontier Lifecycle::SuspendableParticipant runtime extension uniting DeformableWorldRegistry,
//    DeformableVulkanExecutor, DeformableThreadExchange, and VehicleDeformableCoupling.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D19)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/Deformable/DeformableThreadExchange.h"
#include "Engine/SimulationSpace/Deformable/DeformableVulkanExecutor.h"
#include "Engine/SimulationSpace/Deformable/DeformableWorldRegistry.h"
#include "Engine/SimulationSpace/Deformable/VehicleDeformableCoupling.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                               EXTENSION RUNTIME PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

struct DeformableExtensionParameters
{
    float                        FixedStepHz              = 60.0f;
    uint32_t                     MaxSubstepsPerTick       = 4u;
    DeformablePresetKind         HeroExchangePreset       = DeformablePresetKind::SportTireFem;
    DeformableWorldCullingPolicy WorldCullingPolicy       {};
    VehicleTireLodPolicy         VehicleLodPolicy         {};
    std::filesystem::path        ShippedVertexAtlasPath   {};
    std::filesystem::path        CompiledShaderDirectory  = "CompiledShaders";
    bool                         EnableVehicleCoupling    = true;
};

struct DeformableExtensionTelemetry
{
    uint64_t                   CompletedTicks         = 0ull;
    double                     AccumulatedSeconds     = 0.0;
    DeformableWorldStepSummary LastWorldSummary       {};
    VehicleCouplingSummary     LastVehicleSummary     {};
    uint64_t                   HeroPublishedGeneration = 0ull;
    bool                       HasShippedVertexAtlas  = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   DEFORMABLE EXTENSION
//------------------------------------------------------------------------------------------------------------------------

/// Deterministic Frontier runtime lifecycle participant owning world deformables, triple-buffered Vulkan executor,
/// wait-free cross-thread hero exchange, and 4-wheel vehicle tire coupling.
class DeformableExtension
{
public:
    DeformableExtension() noexcept = default;
    explicit DeformableExtension(DeformableExtensionParameters initialParameters) noexcept;
    ~DeformableExtension() noexcept;

    DeformableExtension(const DeformableExtension&)            = delete;
    DeformableExtension& operator=(const DeformableExtension&) = delete;
    DeformableExtension(DeformableExtension&&)                 = delete;
    DeformableExtension& operator=(DeformableExtension&&)      = delete;

    // Frontier::Lifecycle::SuspendableParticipant contract
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double deltaSeconds) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool QueryInitialized() const noexcept { return Initialized_; }
    [[nodiscard]] bool QuerySuspended() const noexcept { return Suspended_; }
    void               SetShouldUpdate(bool shouldUpdate) noexcept { ShouldUpdate_ = shouldUpdate; }
    [[nodiscard]] bool QueryShouldUpdate() const noexcept { return ShouldUpdate_; }

#if FRONTIER_DEFORMABLE_HAS_VULKAN
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* interchange,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    // Camera, external ForceField/Jolt coupling, and live vehicle chassis state setters
    void AssignCameraWorldPosition(const std::array<float, 3>& cameraPositionMeters) noexcept;
    void AssignExternalCoupling(const DeformableExternalCoupling* externalCoupling) noexcept;
    void AssignVehicleChassisInput(const VehicleChassisCouplingInput& chassisInput) noexcept;
    void AssignWindPulse(float windPulse) noexcept;

    // Subsystem accessors (safe on owner/simulation thread)
    [[nodiscard]] DeformableWorldRegistry&         AccessWorldRegistry() noexcept { return WorldRegistry_; }
    [[nodiscard]] const DeformableWorldRegistry&   InspectWorldRegistry() const noexcept { return WorldRegistry_; }
    [[nodiscard]] VehicleDeformableCoupling&       AccessVehicleCoupling() noexcept { return VehicleCoupling_; }
    [[nodiscard]] const VehicleDeformableCoupling& InspectVehicleCoupling() const noexcept { return VehicleCoupling_; }
    [[nodiscard]] DeformableVulkanExecutor&        AccessVulkanExecutor() noexcept { return VulkanExecutor_; }
    [[nodiscard]] const DeformableVulkanExecutor&  InspectVulkanExecutor() const noexcept { return VulkanExecutor_; }
    [[nodiscard]] const DeformableBody&            InspectHeroGpuBody() const noexcept { return HeroGpuBody_; }

    // Wait-free cross-thread reading & lock-free directive intake
    [[nodiscard]] bool EnqueueHeroDirective(DeformableControlDirective directive) noexcept;
    [[nodiscard]] Threading::ReadingLease<DeformablePublishedReading> AcquireHeroReadLease() const noexcept;

    [[nodiscard]] const DeformableExtensionTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }
    void RestoreClockState(uint64_t completedTicks, double accumulatedSeconds, uint64_t worldTickIndex) noexcept
    {
        Telemetry_.CompletedTicks     = completedTicks;
        Telemetry_.AccumulatedSeconds = accumulatedSeconds;
        StepAccumulator_              = 0.0;
        WorldRegistry_.RestoreWorldTickIndex(worldTickIndex);
    }

private:
    DeformableExtensionParameters               Parameters_       {};
    DeformableWorldRegistry                     WorldRegistry_    {};
    DeformableVulkanExecutor                    VulkanExecutor_   {};
    DeformableBody                              HeroGpuBody_      {};
    DeformableSimulationConfiguration           HeroGpuConfig_    {};
    VehicleDeformableCoupling                   VehicleCoupling_  {};
    std::unique_ptr<DeformableThreadExchange>   HeroExchange_     {};
    VehicleChassisCouplingInput                 VehicleInput_     {};
    std::array<float, 3>                        CameraWorldPos_   { 0.0f, 2.0f, 6.0f };
    const DeformableExternalCoupling*           ExternalCoupling_ = nullptr;
    float                                       WindPulse_        = 0.0f;
    double                                      StepAccumulator_  = 0.0;
    DeformableExtensionTelemetry                Telemetry_        {};
    bool                                        Initialized_      = false;
    bool                                        Suspended_        = false;
    bool                                        ShouldUpdate_     = true;
};

static_assert(Lifecycle::SuspendableParticipant<DeformableExtension>,
    "DeformableExtension must satisfy Frontier::Lifecycle::SuspendableParticipant");

} // namespace Frontier::SimulationSpace::Deformable
