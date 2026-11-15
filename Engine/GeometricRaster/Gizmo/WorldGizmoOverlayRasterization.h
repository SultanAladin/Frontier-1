/*============================================================================================================================================
                                                       WORLDGIZMOOVERLAYRASTERIZATION.H
============================================================================================================================================*/
// 🧩 Vulkan GPU .slang GizmoRasterization unit overlaying 3D Slate TransformGizmo onto TieredWorldRenderSequence RGBA8 buffers + GPU PickID

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class WorldGizmoOverlayRasterization
{
public:
    WorldGizmoOverlayRasterization() = default;
    ~WorldGizmoOverlayRasterization();

    WorldGizmoOverlayRasterization(const WorldGizmoOverlayRasterization&)            = delete;
    WorldGizmoOverlayRasterization& operator=(const WorldGizmoOverlayRasterization&) = delete;

    [[nodiscard]] bool InitializeOverlay(
        VulkanInterchange*           BoundDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownOverlay();

    [[nodiscard]] bool QueryReady() const noexcept { return Ready; }

    [[nodiscard]] bool RasterizeGizmoOntoWorldBuffer(
        std::vector<uint8_t>&         InOutDisplayRgba8,
        uint32_t                      WidthPx,
        uint32_t                      HeightPx,
        const PerspectiveCameraFrame& Camera,
        const Editor::TransformGizmo& Gizmo3D);

    [[nodiscard]] Editor::GizmoHandleOrdinal QueryLastGpuPickAtPixel(uint32_t PixelX, uint32_t PixelY) const noexcept;

private:
    struct GraphicsProgramUnit
    {
        VkShaderModule        VertexSpirvBinary    = VK_NULL_HANDLE;
        VkShaderModule        FragmentSpirvBinary    = VK_NULL_HANDLE;
        VkDescriptorSetLayout SlotLayout    = VK_NULL_HANDLE;
        VkPipelineLayout      ProgramLayout = VK_NULL_HANDLE;
        VkPipeline            ProgramHandle = VK_NULL_HANDLE;
    };

    [[nodiscard]] bool EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx);
    void               ReleaseRenderTargets();
    [[nodiscard]] bool ConstructGizmoProgram(
        const std::filesystem::path& VertSpirv,
        const std::filesystem::path& FragSpirv,
        bool                         DepthTestWrite,
        GraphicsProgramUnit&         OutUnit);

    VulkanInterchange*    DeviceInterchange           = nullptr;
    VkRenderPass          OverlayRenderTargetFormat   = VK_NULL_HANDLE;
    VkDescriptorPool      OverlayDescriptorAllocator  = VK_NULL_HANDLE;
    GraphicsProgramUnit   GizmoDepthProgram           {};
    GraphicsProgramUnit   GizmoTranslucentProgram     {};
    DeviceBufferResource  UniformBuffer               {};
    DeviceBufferResource  VertexBuffer                {};
    DeviceBufferResource  UploadReadbackRgbaBuffer    {};
    DeviceBufferResource  ReadbackPickBuffer          {};
    DeviceImageResource   ColorAttachment             {};
    DeviceImageResource   PickAttachment              {};
    DeviceImageResource   DepthAttachment             {};
    VkFramebuffer         TargetFramebuffer           = VK_NULL_HANDLE;
    uint32_t              TargetWidthPx               = 0u;
    uint32_t              TargetHeightPx              = 0u;
    std::vector<uint32_t> CachedPickOrdinals          {};
    bool                  Ready                       = false;
};

} // namespace Frontier::GeometricRaster
