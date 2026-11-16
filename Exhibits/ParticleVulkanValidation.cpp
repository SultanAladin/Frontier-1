/*============================================================================================================================================
                                                        PARTICLEVULKANVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Real Vulkan 1.3 / SwiftShader compute validation: verifies all 21 SPIR-V modules & dispatches Advect + Reduction kernels on GPU

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSimulationSession.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"
#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"
#include "Engine/GeometricRaster/Compute/ParticleVulkanExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int gPass = 0;
int gFail = 0;

void Expect(bool Cond, const char* Label)
{
    if (Cond) { ++gPass; std::cout << "[PASS] " << Label << "\n"; }
    else      { ++gFail; std::cerr << "[FAIL] " << Label << "\n"; }
}

} // namespace

int main()
{
    std::cout << "=== ParticleSpace Vulkan 1.3 / SwiftShader Compute Validation ===\n";

    ParticleSpace::ParticleVulkanExecutor Executor {};
    std::string Diag;

#ifdef FRONTIER_COMPILED_SHADER_DIR
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#else
    const std::filesystem::path ShaderDir = "build/CompiledShaders";
#endif
    Expect(Executor.Initialize(ShaderDir, Diag), "Verify all 21 compiled ParticleSpace SPIR-V 1.3 modules");
    std::cout << "  " << Diag << "\n";
    Expect(Executor.InspectTelemetry().CompiledShaderCount == 21u, "Exactly 21 SPIR-V binaries validated");
    Expect(Executor.InspectArtifacts().AllShadersPresentOnDisk(),  "All 21 artifact paths exist on disk");

    // Locate repo pinned SwiftShader Vulkan 1.3 ICD
#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path SwiftShaderDir = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "ExternalPackages/VulkanCompute/SwiftShader";
#else
    const std::filesystem::path SwiftShaderDir = "ExternalPackages/VulkanCompute/SwiftShader";
#endif

    const bool GpuReady = Executor.InitializeGpuComputeDevice(SwiftShaderDir, Diag);
    std::cout << "  GPU Device Init: " << Diag << "\n";
    Expect(GpuReady, "Initialized real Vulkan 1.3 compute device (Hardware or SwiftShader)");
    Expect(Executor.InspectTelemetry().HardwareDeviceActive, "Vulkan 1.3 Advect + Reduction compute pipelines compiled cleanly");

    // Prepare 512 live particles via ParticleSimulationSession + active Vector Field vortex force
    ParticleSpace::ParticleDocument Doc = ParticleSpace::InstantiateDocumentFromPresetId("curl-fur-field");
    Doc.Rigs[0].SlotCapacity = 512u;
    const ParticleSpace::ParticleRig& Rig = Doc.Rigs[0];

    SimulationSpace::ForceFields::ForceFieldAccumulator ForceAcc {};
    SimulationSpace::ForceFields::ForceSourceRecordEntry Vortex {};
    Vortex.UniqueToken           = 77u;
    Vortex.Enabled               = true;
    Vortex.Model                 = SimulationSpace::ForceFields::ForceModel::RotationalColumn;
    Vortex.Strength              = 4.5f;
    Vortex.RadiusMeters          = 8.0f;
    Vortex.CentreMeters          = { 0.0f, 1.5f, 0.0f, 0.0f };
    Vortex.EvaluatedCentreMeters = Vortex.CentreMeters;
    ForceAcc.AssignSources({ &Vortex, 1u });

    ParticleSpace::ParticleSimulationSession SeedExt {};
    (void)SeedExt.Initialize(Doc);
    SeedExt.TriggerBurst(0u, 360u);
    for (uint32_t F = 0u; F < 20u; ++F)
    {
        SeedExt.Tick(1.0 / 60.0);
    }

    // Snapshot identical slots before dispatching real Vulkan 1.3 compute advection!
    const auto SeedSpan = SeedExt.InspectParticleSlots();
    std::vector<ParticleSpace::ParticleSlot> CpuSlots(SeedSpan.begin(), SeedSpan.end());
    std::vector<ParticleSpace::ParticleSlot> GpuSlots = CpuSlots;
    const auto BeforeMetrics = ParticleSpace::ParticleMetricsCollector::CollectFromSlots(GpuSlots);
    Expect(BeforeMetrics.LiveCount > 100u, "Seeded >100 live particles for Vulkan compute dispatch");

    ParticleSpace::ParticleStepContext StepCtx { Doc, ForceAcc, 1.0f / 60.0f, 0.5f };
    for (int Step = 0; Step < 5; ++Step)
    {
        StepCtx.ElapsedSeconds += StepCtx.DeltaSeconds;
        Expect(Executor.DispatchAdvectComputeGpu(Rig, StepCtx, GpuSlots, Diag), "vkCmdDispatch ParticleAdvectCompute.comp.spv succeeded");
    }

    // Verify positions actually advected on GPU with zero NaNs
    uint32_t MovedCount = 0u;
    for (std::size_t I = 0u; I < GpuSlots.size(); ++I)
    {
        if (ParticleSpace::QueryParticleSlotAlive(GpuSlots[I]) && ParticleSpace::QueryParticleSlotAlive(CpuSlots[I]))
        {
            const float Dx = GpuSlots[I].PositionMeters[0] - CpuSlots[I].PositionMeters[0];
            const float Dy = GpuSlots[I].PositionMeters[1] - CpuSlots[I].PositionMeters[1];
            const float Dz = GpuSlots[I].PositionMeters[2] - CpuSlots[I].PositionMeters[2];
            if (std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) > 1e-4f) ++MovedCount;
        }
    }
    Expect(MovedCount > 80u, "GPU Advect compute shader moved live particles cleanly");

    // Dispatch real GPU atomic reduction & verify exact parity with CPU ParticleMetricsCollector!
    ParticleSpace::ParticleSimulationMetrics GpuMetrics {};
    Expect(Executor.DispatchReductionComputeGpu(GpuSlots, GpuMetrics, Diag), "vkCmdDispatch ParticleReductionCompute.comp.spv succeeded");
    const ParticleSpace::ParticleSimulationMetrics CpuMetrics = ParticleSpace::ParticleMetricsCollector::CollectFromSlots(GpuSlots);

    std::cout << "  GPU Reduction -> Live=" << GpuMetrics.LiveCount
              << ", MeanSpeed=" << GpuMetrics.MeanSpeedMps << " m/s"
              << ", MaxSpeed="  << GpuMetrics.MaxSpeedMps  << " m/s"
              << " (" << GpuMetrics.LastStepMillis << " ms on " << Executor.InspectTelemetry().DeviceName << ")\n";

    Expect(GpuMetrics.LiveCount == CpuMetrics.LiveCount, "GPU atomic LiveCount matches CPU ground truth EXACTLY");
    Expect(std::fabs(GpuMetrics.MeanSpeedMps - CpuMetrics.MeanSpeedMps) < 0.05f, "GPU atomic MeanSpeedMps matches CPU within 0.05 m/s");
    Expect(std::fabs(GpuMetrics.MaxSpeedMps  - CpuMetrics.MaxSpeedMps)  < 0.05f, "GPU atomic MaxSpeedMps matches CPU within 0.05 m/s");
    Expect(CpuMetrics.NaNOrInfCount == 0u, "Zero NaN/Inf particle states after Vulkan 1.3 GPU compute steps");
    Expect(Executor.InspectTelemetry().GpuComputeDispatches == 6u, "Telemetry recorded 6 real Vulkan GPU compute dispatches");

    Executor.Shutdown();
    std::cout << "Summary: " << gPass << " passed, " << gFail << " failed.\n";
    return (gFail == 0) ? 0 : 1;
}
