/*============================================================================================================================================
                                                          CAMERAOPTICSENCODER.H
============================================================================================================================================*/
// 🧩 Vulkan .slang GPU compute encoder executing CameraVisualEffects.comp.spv (DoF, ChromaticAberration, LensFlare, Vignette & Guides)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::Components::VisualEffects
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) GPUCameraOpticsUniforms
{
    uint32_t ResolutionAndMask[4]    { 0u, 0u, 0u, 0u };
    float    LensAndFocus[4]         { 50.0f, 2.8f, 4.6f, 24.0f };
    float    FlarePositionAndGain[4] { 0.28f, 0.34f, 0.65f, 0.55f };
    float    GhostAndAberration[4]   { 0.45f, 0.65f, 0.22f, 0.42f };
    float    VignetteAndBlur[4]      { 0.48f, 0.75f, 10.0f, 0.0f };
};

class CameraOpticsEncoder
{
public:
    CameraOpticsEncoder() = default;
    ~CameraOpticsEncoder();

    CameraOpticsEncoder(const CameraOpticsEncoder&)            = delete;
    CameraOpticsEncoder& operator=(const CameraOpticsEncoder&) = delete;

    [[nodiscard]] bool InitializeOpticsEncoder(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const std::filesystem::path&        CompiledShaderDirectory);

    void TeardownOpticsEncoder() noexcept;

    [[nodiscard]] static uint32_t EvaluateActiveEffectMask(const CameraConfiguration& Config) noexcept;

    // Executes CameraVisualEffects.comp.spv on the GPU and writes RGBA8 output into InOutRGBA8 (4 * WidthPx * HeightPx bytes).
    [[nodiscard]] bool ExecuteGPUOptics(
        uint32_t                   WidthPx,
        uint32_t                   HeightPx,
        const CameraConfiguration& Config,
        std::span<const float>     OptionalLinearDepthMeters,
        std::vector<uint8_t>&      InOutRGBA8);

private:
    GeometricRaster::VulkanInterchange*       DeviceInterchange = nullptr;
    GeometricRaster::ComputeDispatchEncoder   ComputeEncoder    {};
    GeometricRaster::TimelineTransferRegister TransferRegister  {};
    uint32_t                                  OpticsProgram     = UINT32_MAX;
    GeometricRaster::DeviceBufferResource     UniformBuffer     {};
    GeometricRaster::DeviceBufferResource     SourceBuffer      {};
    GeometricRaster::DeviceBufferResource     DepthBuffer       {};
    GeometricRaster::DeviceBufferResource     OutputBuffer      {};
};

} // namespace Frontier::Components::VisualEffects
