/*============================================================================================================================================
                                                         PARTICLEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Vulkan 1.3 SPIR-V compute & raster pipeline registry/executor with deterministic fallback to ParticleSoftwareExecutor

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticlePipelineArtifacts
{
    std::filesystem::path ReleaseComputeSpv {};
    std::filesystem::path AdvectComputeSpv  {};
    std::filesystem::path SpriteVertexSpv   {};
    std::filesystem::path SpriteFragmentSpv {};
    std::filesystem::path RibbonVertexSpv   {};
    std::filesystem::path RibbonFragmentSpv {};

    [[nodiscard]] bool AllShadersPresentOnDisk() const noexcept;
};

struct ParticleVulkanTelemetry
{
    bool     ShadersVerified      = false;
    bool     HardwareDeviceActive = false;
    uint64_t TotalDispatches      = 0u;
    uint32_t CompiledShaderCount  = 0u;
    uint32_t TotalSpirvBytes      = 0u;
};

/// Orchestrates Vulkan 1.3 SPIR-V particle compute/raster shaders and transparently bridges to ParticleSoftwareExecutor when headless.
class ParticleVulkanExecutor
{
public:
    ParticleVulkanExecutor() = default;

    [[nodiscard]] bool Initialize(const std::filesystem::path& CompiledShaderDirectory, std::string& OutDiagnostic);
    void               Shutdown() noexcept;

    [[nodiscard]] uint32_t ExecuteRigStep(
        const ParticleRig&         Rig,
        const ParticleStepContext& Context,
        std::span<ParticleSlot>    RigSlots,
        std::span<StrandVertex>    OptionalStrandHistory = {});

    [[nodiscard]] const ParticlePipelineArtifacts& InspectArtifacts() const noexcept;
    [[nodiscard]] const ParticleVulkanTelemetry&   InspectTelemetry() const noexcept;

private:
    ParticlePipelineArtifacts Artifacts {};
    ParticleVulkanTelemetry   Telemetry {};
};

} // namespace Frontier::ParticleSpace
