/*============================================================================================================================================
                                                           VECTORCANVASRASTERIZER.H
============================================================================================================================================*/
// 🧩 Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer driven by ThorVG symbolic DOM/typography paths + Planar2D TransformGizmo + R32G32B32A32_UINT picking.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Picking/ViewportPickQuery.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VectorCanvasLayoutMetrics
{
    float TileMinX    = 0.0f;   // [px] - Top-left X of [0..DocExtentPx] tile inside viewport framebuffer
    float TileMinY    = 0.0f;   // [px] - Top-left Y of [0..DocExtentPx] tile inside viewport framebuffer
    float TileSpanPx  = 512.0f; // [px] - Rendered pixel extent of square tile on viewport
    float DocExtentPx = 512.0f; // [px] - Canonical vector document coordinate domain [0..512]
};

struct VectorGpuEvaluationMetrics
{
    uint32_t TotalInstancesEvaluated = 0u;
    float    MinNoiseScale           = 1.0f;
    float    MaxNoiseScale           = 1.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     RASTERIZER CLASS
//------------------------------------------------------------------------------------------------------------------------

class VectorCanvasRasterizer
{
public:
    VectorCanvasRasterizer() = default;
    ~VectorCanvasRasterizer();

    VectorCanvasRasterizer(const VectorCanvasRasterizer&)            = delete;
    VectorCanvasRasterizer& operator=(const VectorCanvasRasterizer&) = delete;

    [[nodiscard]] bool InitializeRasterizer(
        VulkanInterchange*           BoundDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownRasterizer() noexcept;

    [[nodiscard]] bool RasterizeVectorCanvas(
        uint32_t                                        WidthPx,
        uint32_t                                        HeightPx,
        const Workspaces::PatternStampWorkspaceSession& Session,
        const Editor::TransformGizmo*                   OptionalGizmo2D = nullptr,
        std::vector<uint8_t>*                           OptionalOutRGBA = nullptr);

    [[nodiscard]] bool RenderThumbnailRGBA(
        uint32_t                                        ThumbWidthPx,
        uint32_t                                        ThumbHeightPx,
        const Workspaces::PatternStampWorkspaceSession& Session,
        std::vector<uint8_t>&                           OutRGBA);

    [[nodiscard]] ViewportPickHit Pick2DPixel(int32_t CursorX, int32_t CursorY);

    [[nodiscard]] RegionPickSummary Reduce2DRegionPick(
        uint32_t MinX,
        uint32_t MinY,
        uint32_t MaxX,
        uint32_t MaxY);

    [[nodiscard]] static VectorCanvasLayoutMetrics ComputeTileLayoutMetrics(
        uint32_t                                        WidthPx,
        uint32_t                                        HeightPx,
        const Workspaces::PatternStampWorkspaceSession& Session) noexcept;

    static void ConvertScreenToDocumentPx(
        const VectorCanvasLayoutMetrics& Metrics,
        float                            ScreenX,
        float                            ScreenY,
        float&                           OutDocX,
        float&                           OutDocY) noexcept;

    static void ConvertDocumentToNdc(
        const VectorCanvasLayoutMetrics& Metrics,
        uint32_t                         ViewportWidthPx,
        uint32_t                         ViewportHeightPx,
        float                            DocX,
        float                            DocY,
        float&                           OutNdcX,
        float&                           OutNdcY) noexcept;

    [[nodiscard]] VectorGpuEvaluationMetrics QueryLastMetrics() const noexcept { return LastMetrics; }
    [[nodiscard]] uint32_t                   QueryLatestWidthPx() const noexcept { return FinalTarget.WidthPx; }
    [[nodiscard]] uint32_t                   QueryLatestHeightPx() const noexcept { return FinalTarget.HeightPx; }
    [[nodiscard]] const std::vector<uint8_t>& InspectLatestColorBytes() const noexcept { return CachedColorBytes; }

private:
    struct GraphicsProgramBundle
    {
        VkShaderModule        VertBinary    = VK_NULL_HANDLE;
        VkShaderModule        FragBinary    = VK_NULL_HANDLE;
        VkDescriptorSetLayout SlotLayout    = VK_NULL_HANDLE;
        VkPipelineLayout      ProgramLayout = VK_NULL_HANDLE;
        VkPipeline            ProgramHandle = VK_NULL_HANDLE;
    };

    struct LayerScratchTargets
    {
        DeviceImageResource LayerCoverColor   {};
        DeviceImageResource LayerCoverPick    {};
        DeviceImageResource LayerDepthStencil {};
        DeviceImageResource StackedColor      {};
        DeviceImageResource StackedPbrPackA   {};
        DeviceImageResource StackedPbrPackB   {};
        DeviceImageResource StackedPbrPackC   {};
        DeviceImageResource StackedPbrPackD   {};
        DeviceImageResource StackedPbrExtra   {};
        DeviceImageResource StackedPick       {};
        DeviceImageResource SurfacePreview    {};
        VkFramebuffer       LayerFramebuffer  = VK_NULL_HANDLE;
        uint32_t            ExtentPx          = 0u;
    };

    struct FinalMRTFramebuffer
    {
        DeviceImageResource ColorImage  {};
        DeviceImageResource PickImage   {};
        DeviceImageResource DepthImage  {};
        VkFramebuffer       Framebuffer = VK_NULL_HANDLE;
        VkImageLayout       PickLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t            WidthPx     = 0u;
        uint32_t            HeightPx    = 0u;
    };

    struct CompiledLayerGeometry
    {
        struct ShapeBatchSegment
        {
            uint32_t FillFirstVertex   = 0u;
            uint32_t FillVertexCount   = 0u;
            uint32_t StrokeFirstVertex = 0u;
            uint32_t StrokeVertexCount = 0u;
            bool     EvenOddFillRule   = false;
            bool     LocalCoords       = true;
            float    BoundsMinX        = 0.0f;
            float    BoundsMinY        = 0.0f;
            float    BoundsMaxX        = 0.0f;
            float    BoundsMaxY        = 0.0f;
        };

        std::vector<ShapeBatchSegment> Segments     {};
        DeviceBufferResource           VertexBuffer {};
        uint32_t                       TotalVerts   = 0u;
    };

    [[nodiscard]] bool ExecuteCanvasRasterization(
        uint32_t                                        WidthPx,
        uint32_t                                        HeightPx,
        uint32_t                                        ScratchExtentPx,
        bool                                            IncludeGuidesGizmo,
        const Workspaces::PatternStampWorkspaceSession& Session,
        const Editor::TransformGizmo*                   OptionalGizmo2D,
        std::vector<uint8_t>*                           OutRGBA);

    [[nodiscard]] bool EnsureLayerScratch(uint32_t ExtentPx);
    void               ReleaseLayerScratch() noexcept;
    [[nodiscard]] bool EnsureFinalTarget(uint32_t WidthPx, uint32_t HeightPx);
    void               ReleaseFinalTarget() noexcept;

    [[nodiscard]] bool ConstructFramebufferFormats();
    [[nodiscard]] bool ConstructStencilCoverProgram(
        const std::filesystem::path& VertSpirv,
        const std::filesystem::path& FragSpirv,
        uint32_t                     StencilVariant, // 0:EvenOdd, 1:NonZero, 2:FillCover, 3:StrokeCover
        GraphicsProgramBundle&       OutBundle);
    [[nodiscard]] bool ConstructFinalProgram(
        const std::filesystem::path&      VertSpirv,
        const std::filesystem::path&      FragSpirv,
        std::span<const VkDescriptorType> SlotCategories,
        bool                              EnableAlphaBlend,
        GraphicsProgramBundle&            OutBundle);
    void DestroyGraphicsProgram(GraphicsProgramBundle& Bundle) noexcept;

    void RefreshLayerGeometryCache(const Workspaces::PatternStampWorkspaceSession& Session);
    void ReleaseLayerGeometryCache() noexcept;

    VulkanInterchange*        DeviceInterchange             = nullptr;
    ThorVGVectorFrontend      VectorFrontend                {};
    ComputeDispatchEncoder    ComputeEncoder                {};
    TimelineTransferRegister  TransferRegister              {};
    ViewportPickQuery         PickQuery                     {};

    VkFormat                  DepthStencilFormat            = VK_FORMAT_D24_UNORM_S8_UINT;
    VkRenderPass              LayerCoverAttachmentFormat    = VK_NULL_HANDLE;
    VkRenderPass              LayerCoverContinueFormat      = VK_NULL_HANDLE;
    VkRenderPass              FinalMRTAttachmentFormat      = VK_NULL_HANDLE;
    VkDescriptorPool          GraphicsDescriptorAllocator   = VK_NULL_HANDLE;
    VkSampler                 LinearClampSampler            = VK_NULL_HANDLE;

    GraphicsProgramBundle     StencilEvenOddProgram         {};
    GraphicsProgramBundle     StencilNonZeroProgram         {};
    GraphicsProgramBundle     ShapeFillCoverProgram         {};
    GraphicsProgramBundle     ShapeStrokeCoverProgram       {};
    GraphicsProgramBundle     GuideOverlayProgram           {};
    GraphicsProgramBundle     Gizmo2DProgram                {};

    uint32_t                  ModifierExpandComputeProgram  = UINT32_MAX;
    uint32_t                  LayerStackComputeProgram      = UINT32_MAX;
    uint32_t                  PbrSurfaceComputeProgram      = UINT32_MAX;

    LayerScratchTargets       ScratchTargets                {};
    FinalMRTFramebuffer       FinalTarget                   {};

    DeviceBufferResource      LayerUniformStaging           {};
    DeviceBufferResource      ModifierInputBuffer           {};
    DeviceBufferResource      ModifierParamsBuffer          {};
    DeviceBufferResource      InstanceOutputBuffer          {};
    DeviceBufferResource      SummaryMetricsBuffer          {};
    DeviceBufferResource      SurfaceEvalParamsBuffer       {};
    DeviceBufferResource      OverlayUniformsBuffer         {};
    DeviceBufferResource      GizmoUniformsBuffer           {};
    DeviceBufferResource      GizmoVertexBuffer             {};
    DeviceBufferResource      RegionPickMaskBuffer          {};

    struct PendingAsyncVectorFrame
    {
        VkCommandBuffer Cmd            = VK_NULL_HANDLE;
        ReadbackTicket  SummaryTicket  {};
        uint64_t        TimelineSignal = 0ull;
    };
    void ReapPendingAsyncFrames(bool ForceDrain) noexcept;

    uint64_t                  CachedSessionSerial           = 0ull;
    uint64_t                  CachedGeometryRevision        = 0ull;
    std::unordered_map<uint32_t, CompiledLayerGeometry> CachedLayerGeometry {};
    std::vector<uint8_t>      CachedColorBytes              {};
    std::deque<PendingAsyncVectorFrame> PendingAsyncFrames  {};
    VectorGpuEvaluationMetrics LastMetrics                  {};
};

} // namespace Frontier::GeometricRaster
