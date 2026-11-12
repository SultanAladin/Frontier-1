/*============================================================================================================================================
                                                        PROGRESSIVESTRETCHRELAXER.H
============================================================================================================================================*/
// 🧩 Vulkan .slang GPU Jacobi stretch-guided UV relaxation solver (StretchRelaxCompute.comp.spv) with zero-flip orientation guard

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <filesystem>

namespace Frontier::Workspaces
{

struct StretchRelaxReport
{
    uint32_t WeldedCornersRelaxed = 0u;    // [count] - Unique welded UV corners evaluated per iteration
    uint32_t ExecutedIterations       = 0u;    // [count] - Jacobi relaxation passes executed
    bool     ExecutedOnVulkanGPU  = false; // [-]     - True when dispatched via StretchRelaxCompute.comp.spv
    double   ElapsedMilliseconds  = 0.0;   // [ms]    - Total GPU/CPU relaxation wall time
};

class ProgressiveStretchRelaxer
{
public:
    ProgressiveStretchRelaxer() = default;
    ~ProgressiveStretchRelaxer();

    ProgressiveStretchRelaxer(const ProgressiveStretchRelaxer&)            = delete;
    ProgressiveStretchRelaxer& operator=(const ProgressiveStretchRelaxer&) = delete;

    [[nodiscard]] bool InitializeGPUCompute(
        GeometricRaster::VulkanInterchange* DeviceInterchange,
        const std::filesystem::path&          CompiledShaderDir);

    void TeardownGPUCompute() noexcept;

    StretchRelaxReport RelaxStretch(
        GeometrySpace::PolygonNetwork& Network,
        uint32_t                       RelaxationIterations = 12u,
        float                          StepScale       = 0.35f,
        bool                           PreserveSeams   = true);

private:
    GeometricRaster::VulkanInterchange*   DeviceInterchange          = nullptr;
    GeometricRaster::ComputeDispatchEncoder Encoder            {};
    uint32_t                                RelaxProgramHandle = UINT32_MAX;
};

} // namespace Frontier::Workspaces
