/*====================================================================================================================================
                                                        SKETCHERRASTERIZATION.H
====================================================================================================================================*/
// 🧩 Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherGizmoSpace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Selection-state bits per face / edge (mirrored in Engine/Shaders/Sketcher/SketcherProjection.slang).
constexpr uint32_t StateSelected = 1u;
constexpr uint32_t StateActive   = 2u;
constexpr uint32_t StateHovered  = 4u;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Host mirror of SketcherProjection (128 bytes, column-major float matrices).
struct SketcherProjectionBlock
{
    float ClipFromWorld[16] = {};                                                            // [-]  - Projection × view
    float ViewRight[4]      = {};                                                            // [-]  - w: orthographic flag
    float ViewUp[4]         = {};                                                            // [-]  - Camera up, w: zebra stripe count (0 off)
    float ViewForward[4]    = {};                                                            // [-]  - Eye → target
    float DisplayStroke[4]  = {};                                                            // [px] - Width, height, stroke, depth bias
};
static_assert(sizeof(SketcherProjectionBlock) == 128, "SketcherProjection push constant must stay 128 bytes");

struct SketcherGridBlock
{
    float PlaneFromClip[16]  = {};                                                           // [-]   - Grid coordinates (u, v, n) ← clip
    float AlongU[4]          = {};                                                           // [0-1] - Colour of the axis along u
    float AlongV[4]          = {};                                                           // [0-1] - Colour of the axis along v
    float Reserved[4]        = {};                                                           // [-]   - Padding to 128 bytes
    float ReservedSecond[4]  = {};                                                           // [-]   - Padding to 128 bytes
};
static_assert(sizeof(SketcherGridBlock) == 128, "SketcherGridProjection push constant must stay 128 bytes");

struct SketcherImagePlaneBlock
{
    float ClipFromWorld[16] = {};                                                            // [-]   - Projection × view
    float Corner[4]         = {};                                                            // [len] - Crop top-left, w: opacity
    float Right[4]          = {};                                                            // [len] - Crop width span, w: mode (0 line art)
    float Down[4]           = {};                                                            // [len] - Crop height span
    float Crop[4]           = {};                                                            // [0-1] - u0, v0, u1, v1
};
static_assert(sizeof(SketcherImagePlaneBlock) == 128, "SketcherImagePlaneProjection push constant must stay 128 bytes");

// One image plane to draw this frame: the uploaded picture it samples and its world parallelogram.
struct SketcherImagePlaneDraw
{
    uint32_t              Picture = 0;                                                       // [idx] - Into the last UploadPictures list
    std::array<double, 3> Corner  = {};                                                      // [len] - Crop top-left (world)
    std::array<double, 3> Right   = {};                                                      // [len] - Crop width span
    std::array<double, 3> Down    = {};                                                      // [len] - Crop height span
    std::array<float, 4>  Crop    = { 0.0f, 0.0f, 1.0f, 1.0f };                              // [0-1] - Picture uv rectangle
    float                 Opacity = 0.6f;                                                    // [0-1] - Ink opacity
    float                 Mode    = 0.0f;                                                    // [-]   - 0 line art, 1 photograph
    bool                  FrontOnlyCondition = false;                                      // [-]   - Optional one-sided reference; evaluated per viewport
};

[[nodiscard]] bool FacesSketcherImagePlane(const SketcherImagePlaneDraw& Draw, const OrbitCameraFrame& Frame) noexcept;

struct SketcherViewportStyle
{
    float Background[4]   = { 0.090f, 0.106f, 0.137f, 1.0f };                                // [0-1] - Sketcher viewport #171b23
    float EdgeStroke      = 1.5f;                                                            // [px]  - Edge stroke width
    float EdgeDepthMargin = 3.0f;                                                            // [px]  - Edge pull toward the eye (world length of this many pixels)
    bool  GridEnabled     = true;                                                            // [-]   - Drawing-plane grid
    int   GridPlane       = 0;                                                               // [-]   - 0 XY (Top/Bottom), 1 XZ (Front/Back), 2 YZ (Right/Left)
    bool  EdgesEnabled    = true;                                                            // [-]   - B-rep edges
    bool  SurfacesEnabled = true;                                                            // [-]   - B-rep faces
    float ZebraStripes    = 0.0f;                                                            // [-]   - Zebra stripes over the reflection angle (0 off)
    bool  XRayEnabled     = false;                                                           // [-]   - Alt+Z: ghost faces + occluded edges, picked through
    float XRayOpacity     = 0.45f;                                                           // [0-1] - Ghost face opacity α in x-ray
    bool  SectionEnabled  = false;                                                           // [-]   - Alt+S: clip at SectionPlane, cap the solids
    std::array<double, 4> SectionPlane = { 0.0, 0.0, 1.0, 0.0 };                             // [-]   - (n, w): kept side n·P + w ≥ 0
    std::vector<SketcherImagePlaneDraw> ImagePlanes = {};                                    // [-]   - Blueprint references, depth-tested behind the model
    GizmoGeometry Gizmo        = {};                                                         // [-]   - Transform gizmo (CPU points, drawn last on cleared depth)
};

// One sub-viewport pane inside the shared offscreen Colour / Identity / Depth targets (Single = 1 pane, Split = 2..4 panes).
struct SketcherRasterPane
{
    int                   OffsetX = 0;                                                       // [px] - Scissor/viewport left within Colour target
    int                   OffsetY = 0;                                                       // [px] - Scissor/viewport top within Colour target
    uint32_t              Width   = 0;                                                       // [px] - Pane width
    uint32_t              Height  = 0;                                                       // [px] - Pane height
    OrbitCameraFrame      Frame   = {};                                                      // [-]  - Camera frame for this pane
    SketcherViewportStyle Style   = {};                                                      // [-]  - Per-pane GridPlane + scaled Gizmo
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

// 💡 Borrowed VulkanInterchange; the caller owns submission and synchronization.
//    Record() leaves the colour target in SHADER_READ_ONLY_OPTIMAL for the interface to sample.
class SketcherRasterization
{
public:
    SketcherRasterization() = default;
    SketcherRasterization(const SketcherRasterization&)            = delete;
    SketcherRasterization& operator=(const SketcherRasterization&) = delete;
    ~SketcherRasterization();

    bool Create(
        const GeometricRaster::VulkanInterchange& Device,
        const std::filesystem::path&              CompiledShaderDirectory,
        std::string&                              Refusal,
        uint32_t                                  SampleWish = 1);
    [[nodiscard]] uint32_t SampleCount() const { return uint32_t(Samples); }
    void Destroy();

    // Recreates the targets when the extent changes (waits for the device first).
    void Resize(uint32_t Width, uint32_t Height);
    void UploadScene(const SketcherScene& Scene);
    void AssignStates(const std::vector<uint32_t>& FaceStates, const std::vector<uint32_t>& EdgeStates);
    void UploadPictures(const std::vector<SketcherPicture>& Pictures);
    [[nodiscard]] size_t PictureCount() const { return Pictures.size(); }
    void UploadMatcaps(const std::vector<SketcherPicture>& Layers);
    [[nodiscard]] uint32_t MatcapLayerCount() const { return MatcapLayers; }

    // Records the viewport. ProbeX / ProbeY ≥ 0 copy that identity texel; ReadbackCondition copies both whole targets.
    void Record(VkCommandBuffer Commands, const OrbitCameraFrame& Frame, const SketcherViewportStyle& Style, bool ReadbackCondition,
                int ProbeX, int ProbeY);

    // Records 1 … 4 split-viewport panes into the shared Colour / Identity / Depth targets with per-pane viewport + scissor.
    void RecordPanes(VkCommandBuffer Commands, const std::vector<SketcherRasterPane>& Panes, const float Background[4], bool ReadbackCondition,
                     int ProbeX, int ProbeY);

    // Valid after the recorded work completed.
    [[nodiscard]] uint32_t        ProbedIdentity() const;
    [[nodiscard]] const uint32_t* ReadbackColour() const;                                    // RGBA8, row-major, Width × Height
    [[nodiscard]] const uint32_t* ReadbackIdentity() const;                                  // R32, row-major, Width × Height

    [[nodiscard]] VkImageView ColourView() const { return Colour.ViewHandle; }
    [[nodiscard]] uint64_t    TargetGeneration() const { return BuiltTargets; }
    [[nodiscard]] uint32_t    Width() const { return Colour.WidthPx; }
    [[nodiscard]] uint32_t    Height() const { return Colour.HeightPx; }
    [[nodiscard]] uint64_t    SceneGeneration() const { return UploadedGeneration; }

private:
    struct GpuImage
    {
        VkImage        ImageHandle  = VK_NULL_HANDLE;
        VkDeviceMemory MemoryHandle = VK_NULL_HANDLE;
        VkImageView    ViewHandle   = VK_NULL_HANDLE;
        VkFormat       ImageFormat  = VK_FORMAT_UNDEFINED;
        uint32_t       WidthPx      = 0u;
        uint32_t       HeightPx     = 0u;
    };

    struct PictureTexture
    {
        GpuImage        Image = {};
        VkDescriptorSet Set   = VK_NULL_HANDLE;
    };

    void CreateRenderPass();
    void CreateTargets(uint32_t Width, uint32_t Height);
    void DestroyTargets();
    void CreatePipelines(const std::filesystem::path& CompiledShaderDirectory);
    void ReleasePictures();

    [[nodiscard]] GpuImage CreateGpuImage(
        uint32_t           Width,
        uint32_t           Height,
        VkFormat           Format,
        VkImageUsageFlags  Usage,
        VkImageAspectFlags Aspect,
        uint32_t           ArrayLayers = 0u,
        uint32_t           MipLevels   = 1u) const;
    void DestroyGpuImage(GpuImage& Image) const noexcept;
    [[nodiscard]] GeometricRaster::DeviceBufferResource CreateHostBuffer(VkDeviceSize Size, VkBufferUsageFlags Usage) const;
    void DestroyBuffer(GeometricRaster::DeviceBufferResource& Buffer) const noexcept;
    void SubmitImmediately(const std::function<void(VkCommandBuffer)>& Record) const;

    const GeometricRaster::VulkanInterchange* Device = nullptr;

    GpuImage                              Colour           = {};
    GpuImage                              Identity         = {};
    GpuImage                              Depth            = {};
    VkRenderPass                          RenderPass       = VK_NULL_HANDLE;
    VkFramebuffer                         Framebuffer      = VK_NULL_HANDLE;
    VkSampleCountFlagBits                 Samples          = VK_SAMPLE_COUNT_1_BIT;
    GeometricRaster::DeviceBufferResource ColourReadback   = {};
    GeometricRaster::DeviceBufferResource IdentityReadback = {};
    GeometricRaster::DeviceBufferResource ProbeReadback    = {};
    GeometricRaster::DeviceBufferResource CornerBuffer     = {};
    GeometricRaster::DeviceBufferResource SegmentBuffer    = {};
    GeometricRaster::DeviceBufferResource FaceStateBuffer  = {};
    GeometricRaster::DeviceBufferResource EdgeStateBuffer  = {};
    GeometricRaster::DeviceBufferResource SectionBuffer    = {};
    GeometricRaster::DeviceBufferResource GizmoBuffer      = {};
    uint32_t                              CornerCount      = 0u;
    uint32_t                              SolidCornerCount = 0u;
    uint32_t                              SegmentCount     = 0u;
    uint32_t                              SolidSegmentCount = 0u;
    uint32_t                              FaceCount        = 0u;
    uint32_t                              EdgeCount        = 0u;
    uint64_t                              UploadedGeneration = UINT64_MAX;
    uint64_t                              BuiltTargets       = 0u;

    VkDescriptorSetLayout       BufferLayout                 = VK_NULL_HANDLE;
    VkDescriptorPool            DescriptorPool               = VK_NULL_HANDLE;
    VkDescriptorSet             BufferSet                    = VK_NULL_HANDLE;
    VkPipelineLayout            PipelineLayout               = VK_NULL_HANDLE;
    VkPipeline                  SurfacePipeline              = VK_NULL_HANDLE;
    VkPipeline                  SurfaceDepthPipeline         = VK_NULL_HANDLE;
    VkPipeline                  SurfaceGhostPipeline         = VK_NULL_HANDLE;
    VkPipeline                  CutterDepthPipeline          = VK_NULL_HANDLE;
    VkPipeline                  CutterGhostPipeline          = VK_NULL_HANDLE;
    VkPipeline                  EdgeOccludedPipeline         = VK_NULL_HANDLE;
    VkPipeline                  EdgePipeline                 = VK_NULL_HANDLE;
    VkPipeline                  EdgeIdentityPipeline         = VK_NULL_HANDLE;
    VkPipeline                  EdgeOccludedIdentityPipeline = VK_NULL_HANDLE;
    VkPipeline                  GridPipeline                 = VK_NULL_HANDLE;
    VkPipeline                  GizmoPipeline                = VK_NULL_HANDLE;
    VkDescriptorSetLayout       PictureLayout                = VK_NULL_HANDLE;
    VkDescriptorPool            PicturePool                  = VK_NULL_HANDLE;
    VkSampler                   PictureSampler               = VK_NULL_HANDLE;
    VkPipelineLayout            PlaneLayout                  = VK_NULL_HANDLE;
    VkPipeline                  PlanePipeline                = VK_NULL_HANDLE;
    std::vector<PictureTexture> Pictures                     = {};
    GpuImage                    MatcapArray                  = {};
    VkDescriptorPool            MatcapPool                   = VK_NULL_HANDLE;
    VkDescriptorSet             MatcapSet                    = VK_NULL_HANDLE;
    uint32_t                    MatcapLayers                 = 0u;
};

} // namespace Frontier::Workspaces::Sketcher
