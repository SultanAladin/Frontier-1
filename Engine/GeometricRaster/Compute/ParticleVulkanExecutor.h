/*============================================================================================================================================
                                                           PARTICLEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Real Vulkan 1.3 SPIR-V compute & raster pipeline executor (SwiftShader/hardware GPU dispatch + deterministic CPU bridge)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"
#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticlePipelineArtifacts
{
    std::filesystem::path ReleaseComputeSpv          {};
    std::filesystem::path AdvectComputeSpv           {};
    std::filesystem::path ForceCouplingComputeSpv    {};
    std::filesystem::path SpatialGridComputeSpv      {};
    std::filesystem::path ReductionComputeSpv        {};
    std::filesystem::path SpriteVertexSpv            {};
    std::filesystem::path SpriteFragmentSpv          {};
    std::filesystem::path RibbonVertexSpv            {};
    std::filesystem::path RibbonFragmentSpv          {};
    std::filesystem::path SolidVertexSpv             {};
    std::filesystem::path SolidFragmentSpv           {};
    std::filesystem::path WingedVertexSpv            {};
    std::filesystem::path WingedFragmentSpv          {};
    std::filesystem::path LineArcVertexSpv           {};
    std::filesystem::path LineArcFragmentSpv         {};
    std::filesystem::path LensVertexSpv              {};
    std::filesystem::path LensFragmentSpv            {};
    std::filesystem::path SurfaceBackingVertexSpv    {};
    std::filesystem::path SurfaceBackingFragmentSpv  {};
    std::filesystem::path OverlayVertexSpv           {};
    std::filesystem::path OverlayFragmentSpv         {};

    [[nodiscard]] bool AllShadersPresentOnDisk() const noexcept;
};

struct ParticleVulkanTelemetry
{
    bool        ShadersVerified      = false;
    bool        HardwareDeviceActive = false;
    uint64_t    TotalDispatches      = 0u;
    uint64_t    GpuComputeDispatches = 0u;
    uint32_t    CompiledShaderCount  = 0u;
    uint32_t    TotalSpirvBytes      = 0u;
    std::string DeviceName           {};
};

/// Orchestrates all 21 Vulkan 1.3 SPIR-V particle shaders, executes live GPU compute dispatches via Volk/Vulkan 1.3,
/// and falls back cleanly to ParticleSoftwareExecutor when specialized CPU solvers are active or no GPU context is attached.
class ParticleVulkanExecutor
{
public:
    ParticleVulkanExecutor();
    ~ParticleVulkanExecutor();

    ParticleVulkanExecutor(const ParticleVulkanExecutor&)            = delete;
    ParticleVulkanExecutor& operator=(const ParticleVulkanExecutor&) = delete;

    [[nodiscard]] bool Initialize(const std::filesystem::path& CompiledShaderDirectory, std::string& OutDiagnostic);

    /// Initializes a real headless Vulkan 1.3 compute device (hardware GPU or SwiftShader) & builds Advect + Reduction compute pipelines.
    [[nodiscard]] bool InitializeGpuComputeDevice(const std::filesystem::path& SwiftShaderIcdDir, std::string& OutDiagnostic);

    void Shutdown() noexcept;

    /// Executes one full rig physics step (software bridge for specialized CPU solvers, GPU compute when requested).
    [[nodiscard]] uint32_t ExecuteRigStep(
        const ParticleRig&         Rig,
        const ParticleStepContext& Context,
        std::span<ParticleSlot>    RigSlots,
        std::span<StrandVertex>    OptionalStrandHistory = {});

    /// Dispatches ParticleAdvectCompute.comp.spv directly on the active Vulkan 1.3 compute queue and reads back updated slots.
    [[nodiscard]] bool DispatchAdvectComputeGpu(
        const ParticleRig&         Rig,
        const ParticleStepContext& Context,
        std::span<ParticleSlot>    RigSlots,
        std::string&               OutDiagnostic);

    /// Dispatches ParticleReductionCompute.comp.spv directly on the active Vulkan 1.3 compute queue and decodes telemetry metrics.
    [[nodiscard]] bool DispatchReductionComputeGpu(
        std::span<const ParticleSlot> RigSlots,
        ParticleSimulationMetrics&    OutMetrics,
        std::string&                  OutDiagnostic);

    [[nodiscard]] const ParticlePipelineArtifacts& InspectArtifacts() const noexcept;
    [[nodiscard]] const ParticleVulkanTelemetry&   InspectTelemetry() const noexcept;

private:
    struct VulkanDeviceState;
    std::unique_ptr<VulkanDeviceState> DeviceState {};
    ParticlePipelineArtifacts          Artifacts   {};
    ParticleVulkanTelemetry            Telemetry   {};
};

} // namespace Frontier::ParticleSpace
