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
    [[nodiscard]] bool QueryGpuCompositeSupported() const noexcept
    {
        return Ready && ViewportBlitProgram.ProgramHandle != VK_NULL_HANDLE && GpuCompositeRenderPass != VK_NULL_HANDLE;
    }

    [[nodiscard]] bool RasterizeGizmoOntoWorldBuffer(
        std::vector<uint8_t>&         InOutDisplayRgba8,
        uint32_t                      WidthPx,
        uint32_t                      HeightPx,
        const PerspectiveCameraFrame& Camera,
        const Editor::TransformGizmo& Gizmo3D);

    struct GpuViewportCompositeRequest
    {
        VkImageView                   WorldSourceView      = VK_NULL_HANDLE;
        uint32_t                      TargetWidthPx        = 0u;
        uint32_t                      TargetHeightPx       = 0u;
        PerspectiveCameraFrame        Camera               {};
        const Editor::TransformGizmo* Gizmo3D              = nullptr;
        bool                          DrawGizmo            = true;
        const uint8_t*                ForceOverlayRgba     = nullptr;
        uint32_t                      ForceOverlayWidthPx  = 0u;
        uint32_t                      ForceOverlayHeightPx = 0u;
        bool                          ForceOverlayEnabled  = false;
        bool                          ForceOverlayUpdated  = false;
        bool                          RequestPickPatch     = false;
        uint32_t                      PickCursorX          = 0u;
        uint32_t                      PickCursorY          = 0u;
    };

    struct GpuViewportCompositeResult
    {
        VkImageView ColorImageView  = VK_NULL_HANDLE;
        uint64_t    ImageGeneration = 0ull;
        uint32_t    WidthPx         = 0u;
        uint32_t    HeightPx        = 0u;
    };

    [[nodiscard]] bool CompositeOntoGpuViewport(
        const GpuViewportCompositeRequest& Request,
        GpuViewportCompositeResult&        OutResult);

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
    void               FlushPreviousCompositeCmd();
    [[nodiscard]] bool ConstructGizmoProgram(
        const std::filesystem::path& VertSpirv,
        const std::filesystem::path& FragSpirv,
        bool                         DepthTestWrite,
        GraphicsProgramUnit&         OutUnit);
    [[nodiscard]] bool ConstructBlitProgram(
        const std::filesystem::path& VertSpirv,
        const std::filesystem::path& FragSpirv,
        GraphicsProgramUnit&         OutUnit);

    VulkanInterchange*    DeviceInterchange           = nullptr;
    VkRenderPass          OverlayRenderTargetFormat   = VK_NULL_HANDLE;
    VkRenderPass          GpuCompositeRenderPass      = VK_NULL_HANDLE;
    VkDescriptorPool      OverlayDescriptorAllocator  = VK_NULL_HANDLE;
    GraphicsProgramUnit   GizmoDepthProgram           {};
    GraphicsProgramUnit   GizmoTranslucentProgram     {};
    GraphicsProgramUnit   ViewportBlitProgram         {};
    VkSampler             BlitLinearSampler           = VK_NULL_HANDLE;
    DeviceBufferResource  UniformBuffer               {};
    DeviceBufferResource  VertexBuffer                {};
    DeviceBufferResource  UploadReadbackRgbaBuffer    {};
    DeviceBufferResource  ReadbackPickBuffer          {};
    DeviceBufferResource  ReadbackPickPatchBuffer     {};
    DeviceBufferResource  ForceOverlayStagingBuffer   {};
    DeviceImageResource   FallbackTransparentImage    {};
    DeviceImageResource   ForceOverlayImage           {};
    bool                  ForceOverlayImageValid      = false;
    DeviceImageResource   ColorAttachment             {};
    DeviceImageResource   PickAttachment              {};
    DeviceImageResource   DepthAttachment             {};
    VkFramebuffer         TargetFramebuffer           = VK_NULL_HANDLE;
    VkFramebuffer         GpuCompositeFramebuffer     = VK_NULL_HANDLE;
    uint32_t              TargetWidthPx               = 0u;
    uint32_t              TargetHeightPx              = 0u;
    uint64_t              ColorAttachmentGeneration   = 0ull;
    VkCommandBuffer       PreviousCompositeCmd        = VK_NULL_HANDLE;
    uint64_t              PreviousCompositeSignal     = 0ull;
    std::vector<uint32_t> CachedPickOrdinals          {};
    std::array<uint32_t, 25u> CachedPickPatch         {};
    uint32_t              CachedPickPatchX0           = 0u;
    uint32_t              CachedPickPatchY0           = 0u;
    uint32_t              CachedPickPatchW            = 0u;
    uint32_t              CachedPickPatchH            = 0u;
    bool                  Ready                       = false;
};

} // namespace Frontier::GeometricRaster
