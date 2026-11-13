/*============================================================================================================================================
                                                         UVWORKSPACERASTERIZER.H
============================================================================================================================================*/
// 🧩 Vulkan .slang 3D Surface/MatCap + 2D UDIM UVTile + 3D/2D TransformGizmo MRT rasterizer with GPU Selection & Stretch compute

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/TransformGizmo.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Picking/ViewportPickQuery.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SurfaceDisplayMode : uint32_t
{
    SurfaceMaterial    = 0u, // [-] - Surface material viewport mode; the MatCap atlas is opt-in
    PriorityLayerTint  = 1u, // [-] - Color-coded by PriorityAllocationLayer
    StretchHeatmap     = 2u, // [-] - Isometric L2 stretch green->red diagnostic
    UDIMCheckerPreview = 3u, // [-] - 3D projected UDIM checker pattern
    MatCapPreview      = 4u, // [-] - Pure baked per-face engineering MatCap preview
    MaterialAndMatCap  = 5u, // [-] - Surface material blended with the baked MatCap slot
};

enum class UVCanvasDisplayMode : uint32_t
{
    PriorityLayerFill  = 0u, // [-] - Color-coded by PriorityAllocationLayer
    StretchHeatmap     = 1u, // [-] - Per-face L2 stretch diagnostic
    WindingAndOverlap  = 2u, // [-] - Flipped-winding crimson & overlap amber validator
};

enum class GPUSelectionOperation : uint32_t
{
    ClearAll          = 0u,
    SelectByLayer     = 1u,
    SelectByPartGroup = 2u,
    SelectSingleFace  = 3u,
    ToggleSingleFace  = 4u,
    SelectAll         = 5u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CameraOrbitRig3D
{
    Mathematics::Axis3D               TargetPivot            { 0.0f, 0.0f, 0.35f };                              // [m]   - Orbit target point
    float                             YawRadians             = 0.68f;                                            // [rad] - Horizontal orbit angle
    float                             PitchRadians           = 0.38f;                                            // [rad] - Elevation angle
    float                             DistanceMeters         = 4.60f;                                            // [m]   - Camera eye distance
    float                             VerticalFovDeg         = 42.0f;                                            // [°]   - Perspective vertical FOV
    bool                              OrthographicProjection = false;                                            // [-]   - True = parallel ortho view
    Mathematics::WorldCoordinateBasis Basis                  = Mathematics::WorldCoordinateBasis::RightHandedZUp;// [-]   - Active RHR world basis
};

struct Viewport3DRenderOptions
{
    CameraOrbitRig3D  Camera              {};
    SurfaceDisplayMode  DisplayMode         = SurfaceDisplayMode::SurfaceMaterial;
    bool                WireframeEnabled    = true;
    bool                SeamOverlayEnabled  = false; // Default OFF per workspace policy!
    bool                GroundGridEnabled   = true;
    bool                RenderGizmo         = true;
    uint16_t            ActiveLayerOrdinal  = 0u;
    uint32_t            HoveredFaceIndex    = GeometrySpace::InvalidNetworkIndex;
};

enum class UVBackdropPreset : uint8_t
{
    None             = 0u, // [-] - Standard checker / dark UDIM tile
    ColorChecker     = 1u, // [-] - 24-patch Studio ColorChecker calibration chart
    TrimSheetAtlas   = 2u, // [-] - Horizontal trim-sheet band atlas preview
    BrushedAnisoPack = 3u, // [-] - Directional brushed metal anisotropic reference
    UVCode           = 5u, // [-] - Coordinate-coded UDIM backing
    Checker          = 6u, // [-] - Neutral checker backing
    Slate            = 7u, // [-] - Flat slate backing
};

struct Canvas2DRenderOptions
{
    Mathematics::UVAxes VisibleMinUV        { -0.06f, -0.06f };
    Mathematics::UVAxes VisibleMaxUV        { 2.06f, 1.06f };
    UVCanvasDisplayMode DisplayMode         = UVCanvasDisplayMode::PriorityLayerFill;
    UVBackdropPreset    BackdropPreset      = UVBackdropPreset::None;
    bool                CheckerEnabled      = true;
    bool                SeamOverlayEnabled  = false; // Default OFF per workspace policy!
    bool                PinOverlayEnabled   = true;  // GPU pin beacons for EnablePinLock corners
    bool                RenderGizmo         = true;
    uint16_t            ActiveLayerOrdinal  = 0u;
    uint32_t            HoveredFaceIndex    = GeometrySpace::InvalidNetworkIndex;
};

struct GPUUVDistortionSummary
{
    float               MeanL2Stretch    = 1.0f; // [-]     - Average normalized L2 stretch (1.0 = isometric)
    uint32_t            FlippedFaceCount = 0u;   // [count] - Faces with SignedAreaUV <= 0
    uint32_t            OverlapFaceCount = 0u;   // [count] - Faces overlapping another island AABB
    std::vector<float>  PerFaceStretch   {};     // [-]     - Readback L2 stretch per face
};

class UVWorkspaceRasterizer
{
public:
    UVWorkspaceRasterizer() = default;
    ~UVWorkspaceRasterizer();

    UVWorkspaceRasterizer(const UVWorkspaceRasterizer&)            = delete;
    UVWorkspaceRasterizer& operator=(const UVWorkspaceRasterizer&) = delete;

    [[nodiscard]] bool InitializeRasterizer(
        VulkanInterchange*         BoundDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownRasterizer() noexcept;

    [[nodiscard]] bool UploadPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        uint32_t                             InstanceSlot = 1u);

    [[nodiscard]] bool DispatchGPUSelection(
        GPUSelectionOperation Operation,
        uint32_t              TargetMask  = 0u,
        bool                  AdditiveMask = false);

    void UploadSelectionBitmaskWords(std::span<const uint32_t> PackedWords);
    [[nodiscard]] std::vector<uint32_t> ReadbackSelectionBitmaskWords();

    [[nodiscard]] GPUUVDistortionSummary EvaluateUVDistortionInspection(
        const GeometrySpace::PolygonNetwork& Network,
        bool                                 CheckOverlaps = true);

    [[nodiscard]] bool Rasterize3DViewport(
        uint32_t                      WidthPx,
        uint32_t                      HeightPx,
        const Viewport3DRenderOptions& Options,
        const Editor::TransformGizmo* OptionalGizmo3D = nullptr,
        std::vector<uint8_t>*         OptionalOutRGBA = nullptr);

    [[nodiscard]] bool Rasterize2DUVCanvas(
        uint32_t                      WidthPx,
        uint32_t                      HeightPx,
        const Canvas2DRenderOptions&  Options,
        const Editor::TransformGizmo* OptionalGizmo2D = nullptr,
        std::vector<uint8_t>*         OptionalOutRGBA = nullptr);

    [[nodiscard]] ViewportPickHit Pick3DPixel(int32_t CursorX, int32_t CursorY);
    [[nodiscard]] ViewportPickHit Pick2DPixel(int32_t CursorX, int32_t CursorY);

    [[nodiscard]] RegionPickSummary Reduce3DRegionPick(
        uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
        uint32_t BrushCenterX = 0u, uint32_t BrushCenterY = 0u, uint32_t BrushRadiusPx = 0u);

    [[nodiscard]] RegionPickSummary Reduce2DRegionPick(
        uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
        uint32_t BrushCenterX = 0u, uint32_t BrushCenterY = 0u, uint32_t BrushRadiusPx = 0u);

    [[nodiscard]] static Editor::ViewportRay3D ConstructCameraRay3D(
        const CameraOrbitRig3D& Camera,
        uint32_t                  WidthPx,
        uint32_t                  HeightPx,
        float                     CursorX,
        float                     CursorY) noexcept;

    [[nodiscard]] ComputeDispatchEncoder& AccessComputeEncoder() noexcept { return ComputeEncoder; }
    [[nodiscard]] TimelineTransferRegister&      AccessTransferRegister() noexcept   { return TransferRegister; }

private:
    struct GraphicsProgramBundle
    {
        VkShaderModule        VertBinary     = VK_NULL_HANDLE;
        VkShaderModule        FragBinary     = VK_NULL_HANDLE;
        VkDescriptorSetLayout SlotLayout      = VK_NULL_HANDLE;
        VkPipelineLayout      ProgramLayout = VK_NULL_HANDLE;
        VkPipeline            ProgramHandle       = VK_NULL_HANDLE;
    };

    struct MRTFramebufferTarget
    {
        DeviceImageResource ColorImage  {};
        DeviceImageResource PickImage   {};
        DeviceImageResource DepthImage  {};
        VkFramebuffer       Framebuffer = VK_NULL_HANDLE;
        VkImageLayout       PickLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t            WidthPx     = 0u;
        uint32_t            HeightPx    = 0u;
    };

    [[nodiscard]] bool ConstructGraphicsProgram(
        const std::filesystem::path&      VertSpirv,
        const std::filesystem::path&      FragSpirv,
        std::span<const VkDescriptorType> SlotCategories,
        bool                              DepthTestAndWrite,
        bool                              EnableColorAlphaBlend,
        GraphicsProgramBundle&            OutBundle);

    void DestroyGraphicsProgram(GraphicsProgramBundle& Bundle) noexcept;
    [[nodiscard]] bool InitializeMatCapAtlas(const std::filesystem::path& CompiledShaderDirectory);
    bool EnsureFramebufferTarget(MRTFramebufferTarget& Target, uint32_t WidthPx, uint32_t HeightPx);
    void ReleaseFramebufferTarget(MRTFramebufferTarget& Target) noexcept;

    VulkanInterchange*    DeviceInterchange                 = nullptr;
    ComputeDispatchEncoder  ComputeEncoder          {};
    TimelineTransferRegister       TransferRegister            {};
    ViewportPickQuery       PickQuery               {};
    VkRenderPass            DualTargetAttachmentFormat    = VK_NULL_HANDLE;
    VkDescriptorPool        GraphicsDescriptorAllocator  = VK_NULL_HANDLE;

    GraphicsProgramBundle  SurfaceProgram         {};
    GraphicsProgramBundle  UVCanvasProgram        {};
    GraphicsProgramBundle  GizmoDepthOnProgram    {};
    GraphicsProgramBundle  GizmoDepthOffProgram   {};

    uint32_t                SelectionComputeProgram = UINT32_MAX;
    uint32_t                StretchComputeProgram   = UINT32_MAX;

    MRTFramebufferTarget    Target3D                {};
    MRTFramebufferTarget    Target2D                {};

    DeviceBufferResource   SurfaceVertexBuffer             {};
    DeviceBufferResource   UVCanvasVertexBuffer             {};
    DeviceImageResource    MatCapAtlasImage                 {};
    VkSampler              MatCapAtlasSampler               = VK_NULL_HANDLE;
    DeviceBufferResource    Gizmo3DVertexBuffer     {};
    DeviceBufferResource    Gizmo2DVertexBuffer     {};
    DeviceBufferResource    FacePackedAttrBuffer    {};
    DeviceBufferResource    SelectionBitmaskBuffer  {};
    DeviceBufferResource    Viewport3DUniformBuffer {};
    DeviceBufferResource    Canvas2DUniformBuffer   {};
    DeviceBufferResource    Gizmo3DUniformBuffer    {};
    DeviceBufferResource    Gizmo2DUniformBuffer    {};
    DeviceBufferResource    SelectionUniformBuffer  {};
    DeviceBufferResource    StretchUniformBuffer    {};
    DeviceBufferResource    StretchInputBuffer      {};
    DeviceBufferResource    StretchOutputBuffer     {};
    DeviceBufferResource    StretchCountersBuffer   {};

    uint32_t                UploadedFaceCount       = 0u;
    uint32_t                SurfaceVertexCount      = 0u;
    uint32_t                GroundGridVertexCount   = 6u;
    uint32_t                UVCanvasVertexCount     = 0u;
    std::vector<float>      CachedFaceStretch       {};
};

} // namespace Frontier::GeometricRaster
