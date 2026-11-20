/*============================================================================================================================================
                                                            CAMERAOPTICSENCODER.H
============================================================================================================================================*/
// 🧩 Vulkan .slang GPU compute encoder executing CameraVisualEffects.comp.spv (DoF, ChromaticAberration, LensFlare, Vignette & Guides)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
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

struct alignas(16) GpuScreenDistortionAnchor
{
    float PosRadKind[4]         { 0.5f, 0.5f, 0.12f, 0.0f }; // x=ScreenU, y=ScreenV, z=RadiusUv, w=Kind(0=Heat,1=BlackHole,2=Glitch)
    float DepthStrengthParam[4] { 4.0f, 1.0f, 0.0f,  0.0f }; // x=LinearDepthM, y=Strength, z=SecondaryParam, w=Active(0/1)
};

struct alignas(16) GPUCameraOpticsUniforms
{
    uint32_t                  ResolutionMaskPacked[4]  { 0u, 0u, 0u, 0u };
    float                     LensFocusPacked[4]       { 50.0f, 2.8f, 4.6f, 24.0f };
    float                     FlarePositionGain[4]     { 0.28f, 0.34f, 0.65f, 0.55f };
    float                     GhostAberrationPacked[4] { 0.45f, 0.65f, 0.22f, 0.42f };
    float                     VignetteBlurPacked[4]    { 0.48f, 0.75f, 10.0f, 0.0f };
    float                     HaloTintPacked[4]        { 0.65f, 1.00f, 0.96f, 0.90f };
    float                     MotionBlurPacked0[4]     { 0.0f, 0.0f, 12.0f, 28.0f };
    float                     MotionBlurPacked1[4]     { 0.0f, 0.0f, 0.50f, 0.0f };
    float                     MotionBlurPacked2[4]     { 0.5f, 0.5f, 0.18f, 1.777778f };
    float                     PrevViewClipRows[4][4]   {};
    float                     CameraOriginTanX[4]      { 0.0f, 0.0f, 0.0f, 0.6f };
    float                     CameraForwardTanY[4]     { 0.0f, 1.0f, 0.0f, 0.4f };
    float                     CameraRightClock[4]      { 1.0f, 0.0f, 0.0f, 0.0f };
    float                     CameraUpReprojValid[4]   { 0.0f, 0.0f, 1.0f, 0.0f };
    float                     HeatShimmerPacked[4]     { 0.0f, 18.0f, 2.4f, 0.28f };
    float                     BlackHolePacked0[4]      { 0.0f, 0.36f, 1.15f, 0.35f };
    float                     BlackHolePacked1[4]      { 0.45f, 1.00f, 0.58f, 0.18f };
    float                     GlitchPacked[4]          { 0.0f, 0.55f, 0.65f, 0.70f };
    float                     DistortionMetaPacked[4]  { 0.0f, 1.0f, 0.0f, 14.0f };
    GpuScreenDistortionAnchor Anchors[8]               {};
};

static_assert(sizeof(GPUCameraOpticsUniforms) == 608u, "GPUCameraOpticsUniforms must equal 608 bytes.");

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

    // Executes CameraVisualEffects.comp.spv with full CameraPosture (enables 3D world-anchor projection & depth-reprojected Motion Blur).
    [[nodiscard]] bool ExecuteGPUOpticsWithPosture(
        uint32_t                   WidthPx,
        uint32_t                   HeightPx,
        const CameraConfiguration& Config,
        const CameraPosture*       OptionalPosture,
        std::span<const float>     OptionalLinearDepthMeters,
        std::vector<uint8_t>&      InOutRGBA8);

private:
    GeometricRaster::VulkanInterchange*       DeviceInterchange = nullptr;
    GeometricRaster::ComputeDispatchEncoder   ComputeEncoder    {};
    GeometricRaster::TimelineTransferRegister TransferRegister  {};
    uint32_t                                  OpticsProgram     = UINT32_MAX;
    GeometricRaster::DeviceBufferResource     UniformBuffer     {};
    GeometricRaster::DeviceBufferResource     SourceBuffer        {};
    GeometricRaster::DeviceBufferResource     DepthBuffer         {};
    GeometricRaster::DeviceBufferResource     OutputBuffer        {};
    GeometricRaster::DeviceBufferResource     BakedFlareAtlasBuffer {};
};

} // namespace Frontier::Components::VisualEffects
