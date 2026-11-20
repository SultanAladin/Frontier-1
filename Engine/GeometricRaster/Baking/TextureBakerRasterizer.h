/*============================================================================================================================================
                                                           TEXTUREBAKERRASTERIZER.H
============================================================================================================================================*/
// 🧩 Interactive Vulkan 3D studio PBR + live baked target + High-Poly wire + Cage overlay rasterizer for Baking Studio viewports

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      CAMERA & VIEW SPEC
//------------------------------------------------------------------------------------------------------------------------

struct BakeOrbitCamera
{
    Mathematics::Axis3D               TargetPivot    { 0.0f, 0.0f, 0.45f };
    float                             YawRadians     = 0.72f;
    float                             PitchRadians   = 0.36f;
    float                             DistanceMeters = 2.15f;
    float                             VerticalFovDeg = 42.0f;
    Mathematics::WorldCoordinateBasis Basis          = Mathematics::WorldCoordinateBasis::RightHandedZUp;
};

struct BakeViewportRenderSpec
{
    uint32_t           WidthPx             = 800u;
    uint32_t           HeightPx            = 600u;
    uint32_t           InspectedTargetId   = 0u;     // 0..16 = 17 surface targets
    bool               SurfaceBaked        = false;
    bool               ShowHighWireOverlay = false;
    bool               ShowCageOverlay     = false;
    float              FrontCageMeters     = 0.14f;
    BakeOrbitCamera    Camera              {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

class TextureBakerRasterizer
{
public:
    TextureBakerRasterizer() = default;
    ~TextureBakerRasterizer();

    TextureBakerRasterizer(const TextureBakerRasterizer&)            = delete;
    TextureBakerRasterizer& operator=(const TextureBakerRasterizer&) = delete;

    [[nodiscard]] bool InitializeRasterizer(
        VulkanInterchange*           BoundDevice,
        GpuTextureBaker*             BoundBaker,
        const std::filesystem::path& ShaderDirectory = {});

    void TeardownRasterizer() noexcept;

    [[nodiscard]] bool RenderSpatialViewport(const BakeViewportRenderSpec& Spec);

    [[nodiscard]] const std::vector<uint8_t>& InspectSpatialRgba8() const noexcept { return SpatialRgbaPixels; }
    [[nodiscard]] uint32_t QuerySpatialWidthPx() const noexcept  { return AllocatedWidthPx; }
    [[nodiscard]] uint32_t QuerySpatialHeightPx() const noexcept { return AllocatedHeightPx; }
    [[nodiscard]] bool     Ready() const noexcept { return Initialized; }

    static void AlignCameraToStudy(const PreparedStudyPair& Study, BakeOrbitCamera& OutCamera) noexcept;

private:
    [[nodiscard]] bool CreatePrograms(const std::filesystem::path& ShaderDir);
    void               DestroyPrograms() noexcept;
    [[nodiscard]] bool EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx);
    void               ReleaseRenderTargets() noexcept;

    VulkanInterchange*     DeviceOwner       = nullptr;
    GpuTextureBaker*       BakerRef          = nullptr;
    bool                   Initialized       = false;

    VkDescriptorPool       DescriptorRegistry  = VK_NULL_HANDLE;
    VkDescriptorSetLayout  PreviewSetLayout    = VK_NULL_HANDLE;
    VkPipelineLayout       PreviewProgramLayout = VK_NULL_HANDLE;
    VkRenderPass           PreviewRenderSequence = VK_NULL_HANDLE;
    VkPipeline             SolidSurfaceProgram = VK_NULL_HANDLE;
    VkPipeline             WireOverlayProgram  = VK_NULL_HANDLE;
    VkDescriptorSet        SolidDescriptorSet  = VK_NULL_HANDLE;
    VkDescriptorSet        HighWireDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet        CageDescriptorSet   = VK_NULL_HANDLE;

    DeviceBufferResource   UniformRingBuffer   {};
    DeviceImageResource    ColorTargetImage    {};
    DeviceImageResource    DepthTargetImage    {};
    VkFramebuffer          PreviewFramebuffer  = VK_NULL_HANDLE;
    DeviceBufferResource   ReadbackBuffer      {};

    uint32_t               AllocatedWidthPx    = 0u;
    uint32_t               AllocatedHeightPx   = 0u;
    std::vector<uint8_t>   SpatialRgbaPixels   {};
};

} // namespace Frontier::GeometricRaster::Baking
