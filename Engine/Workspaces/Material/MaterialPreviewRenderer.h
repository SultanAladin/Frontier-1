/*============================================================================================================================================
                                                          MATERIALPREVIEWRENDERER.H
============================================================================================================================================*/
// 🧩 Lightweight Direct Studio OpenPBR preview renderer (Full 10-lobe MaterialSpace + Studio Softboxes + IBL, ZERO GI overhead)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PreviewSpecimenGeometry : uint8_t
{
    ShaderballPedestal = 0u, // [-] - Calibrated lathe-grooved studio shaderball on dark pedestal
    DrapedClothSheet   = 1u, // [-] - Sculpted hanging cloth wave sheet
    RoundedStudioBox   = 2u, // [-] - Beveled calibration cube
    TurntablePlane     = 3u, // [-] - Flat horizon slab (ideal for ocean water & asphalt puddles)
};

enum class PreviewRenderMode : uint8_t
{
    DirectStudioOpenPbr     = 0u, // [-] - Default fast mode: full 10-lobe OpenPBR + Studio Softboxes + IBL (NO GI)
    RealTimeGiSequence      = 1u, // [-] - Hooks into VulkanWorldRenderSequence Tier 1/2 real-time GI when requested
    DeterministicPathTracer = 2u, // [-] - Hooks into Deterministic Path Tracer reference when requested
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PreviewCameraSetup
{
    float OrbitYawRadians   = 0.35f; // [rad] - Horizontal turntable yaw
    float OrbitPitchRadians = 0.22f; // [rad] - Vertical elevation pitch
    float OrbitDistanceM    = 2.65f; // [m]   - Distance from specimen origin
    float FocalScale        = 0.42f; // [-]   - Vertical half-tangent field of view
};

struct PreviewRenderResult
{
    uint32_t             WidthPx         = 0u;
    uint32_t             HeightPx        = 0u;
    float                RenderMillisec  = 0.0f;
    bool                 UsedVulkanCompute   = false;
    uint32_t             ActiveLobeMask  = 0u;
    std::vector<uint8_t> DisplayRgba8    {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                             MATERIAL PREVIEW RENDERER
//------------------------------------------------------------------------------------------------------------------------

class MaterialPreviewRenderer
{
public:
    MaterialPreviewRenderer() = default;
    ~MaterialPreviewRenderer();

    MaterialPreviewRenderer(const MaterialPreviewRenderer&)            = delete;
    MaterialPreviewRenderer& operator=(const MaterialPreviewRenderer&) = delete;

    // 📝 Optional Vulkan 1.3 GPU initialization binding DynamicMaterialResolve.comp.spv (falls back seamlessly to CPU reference)
    [[nodiscard]] bool InitializeVulkanPreview(
        GeometricRaster::VulkanInterchange& TargetDevice,
        const std::filesystem::path&        CompiledShaderDirectory);

    void TeardownVulkanPreview() noexcept;

    [[nodiscard]] bool QueryVulkanReady() const noexcept { return GpuReady; }

    // 📝 Render any modular DynamicMaterialRecipe using Direct Studio OpenPBR (Full 10-lobe BSDF + Studio IBL, NO GI)
    [[nodiscard]] PreviewRenderResult RenderPreview(
        const MaterialSpace::Dynamic::DynamicMaterialRecipe& Recipe,
        const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
        PreviewSpecimenGeometry                              Specimen,
        PreviewRenderMode                                    RenderMode,
        const PreviewCameraSetup&                            Camera,
        uint32_t                                             WidthPx,
        uint32_t                                             HeightPx);

    // 📝 Execute the compiled Vulkan .slang compute preview shader directly on the bound GPU/SwiftShader adapter
    [[nodiscard]] bool RenderPreviewVulkanCompute(
        const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
        PreviewSpecimenGeometry                              Specimen,
        PreviewRenderMode                                    RenderMode,
        const PreviewCameraSetup&                            Camera,
        uint32_t                                             WidthPx,
        uint32_t                                             HeightPx,
        PreviewRenderResult&                                 OutResult);

    // 📝 Deterministic CPU Direct Studio OpenPBR renderer exercising exact 10-lobe OpenPbrSurfaceScattering + DynamicSurfaceResolver
    [[nodiscard]] static PreviewRenderResult RenderPreviewCpuReference(
        const MaterialSpace::Dynamic::DynamicMaterialRecipe& Recipe,
        const MaterialSpace::Dynamic::DynamicParameterBlock& Parameters,
        PreviewSpecimenGeometry                              Specimen,
        const PreviewCameraSetup&                            Camera,
        uint32_t                                             WidthPx,
        uint32_t                                             HeightPx,
        PreviewRenderMode                                    RenderMode = PreviewRenderMode::DirectStudioOpenPbr);

    // 📝 Raycast a normalized viewport click [0..1]^2 against the active specimen sphere to obtain a local impact direction
    [[nodiscard]] static bool QuerySpecimenHitDirection(
        const PreviewCameraSetup& Camera,
        float                     NormalizedX,
        float                     NormalizedY,
        float                     AspectRatio,
        Mathematics::Axis3D&      OutLocalHitDir) noexcept;

private:
    void EnsureOutputCapacity(uint64_t RequiredBytes);

    GeometricRaster::VulkanInterchange*     BoundDevice        = nullptr;
    GeometricRaster::ComputeDispatchEncoder DispatchEncoder    {};
    uint32_t                                ComputeProgramSlot = UINT32_MAX;
    GeometricRaster::DeviceBufferResource   ViewportUniformBuf {};
    GeometricRaster::DeviceBufferResource   ParamUniformBuf    {};
    GeometricRaster::DeviceBufferResource   PixelOutputBuf     {};
    bool                                    GpuReady           = false;
};

} // namespace Frontier::Workspaces
