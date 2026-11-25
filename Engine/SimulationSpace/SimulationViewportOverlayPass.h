/*============================================================================================================================================
                                                  SIMULATIONVIEWPORTOVERLAYPASS.H
============================================================================================================================================*/
// 🧩 Real Vulkan 1.2 Viewport Composite & Simulation Overlay Pass consuming SimulationGpuRenderFeedPacket
//    (zero CPU readback during live rendering) to draw live GPU Deformable meshes, 3D Fluid volumes, and
//    Particle clouds directly onto the live viewport color image.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/SimulationSpace/SimulationGpuRenderFeed.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#if __has_include(<volk.h>)
    #define FRONTIER_SIM_OVERLAY_HAS_VULKAN 1
    #include "Engine/GeometricRaster/VulkanInterchange.h"
#else
    #define FRONTIER_SIM_OVERLAY_HAS_VULKAN 0
#endif

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAMERA & REQUEST PODS
//------------------------------------------------------------------------------------------------------------------------

struct SimulationOverlayCamera
{
    std::array<float, 3> EyeMeters     { 0.0f, 3.2f, 8.5f };
    std::array<float, 3> TargetMeters  { 0.0f, 1.4f, 0.0f };
    std::array<float, 3> UpAxis        { 0.0f, 1.0f, 0.0f };
    float                VerticalFovRad = 0.78539816f; // 45 deg
    float                NearPlaneMeters = 0.05f;
    float                FarPlaneMeters  = 250.0f;
};

struct SimulationViewportCompositeRequest
{
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    VkImageView                        WorldSourceView = VK_NULL_HANDLE; // Optional 3D World viewport image to composite over
#endif
    uint32_t                           TargetWidthPx    = 0u;
    uint32_t                           TargetHeightPx   = 0u;
    SimulationOverlayCamera            Camera           {};
    SimulationGpuRenderFeedPacket      RenderFeed       {};
    bool                               DrawFluidVolume  = true;
    bool                               DrawParticles    = true;
    bool                               DrawDeformables  = true;
    bool                               ShowStrainHeatmap = true;
    float                              GroundHeightMeters = 0.0f;
    float                              Exposure          = 1.25f;
    float                              ParticleSizeMeters = 0.16f;
    std::array<float, 3>               ParticleColorStart { 1.00f, 0.78f, 0.32f };
    std::array<float, 3>               ParticleColorEnd   { 0.25f, 0.68f, 1.00f };
};

struct SimulationViewportCompositeResult
{
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    VkImageView ColorImageView       = VK_NULL_HANDLE;
#endif
    uint64_t    ImageGeneration      = 0ull;
    uint64_t    SubmittedTimeline    = 0ull;
    uint32_t    WidthPx              = 0u;
    uint32_t    HeightPx             = 0u;
    uint32_t    FluidPassesDrawn     = 0u;
    uint32_t    ParticlePassesDrawn  = 0u;
    uint32_t    DeformablePassesDrawn = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION VIEWPORT OVERLAY PASS
//------------------------------------------------------------------------------------------------------------------------

class SimulationViewportOverlayPass
{
public:
    static constexpr uint32_t kCommandRingSlots = 3u;

    SimulationViewportOverlayPass() = default;
    ~SimulationViewportOverlayPass();

    SimulationViewportOverlayPass(const SimulationViewportOverlayPass&)            = delete;
    SimulationViewportOverlayPass& operator=(const SimulationViewportOverlayPass&) = delete;

#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    [[nodiscard]] bool InitializeOverlay(
        GeometricRaster::VulkanInterchange* boundDevice,
        const std::filesystem::path&        compiledShaderDirectory);
#endif

    void TeardownOverlay() noexcept;

    /// Uploads a cage-bound deformable surface mesh once so DeformableCageSkinning.vert.spv can skin it directly
    /// from the live GPU `PositionBufferHandle` and `CofactorBufferHandle` SSBOs with zero per-frame CPU readback.
    [[nodiscard]] bool UploadDeformableSurfaceMesh(
        const GeometrySpace::Deformation::CageEmbeddedSurface& embeddedSurface,
        uint32_t maxCagePointCount,
        uint32_t maxTetCount);

    /// Records and asynchronously submits the Vulkan 1.2 composite pass (World blit -> Fluid raymarch -> Deformable
    /// cage-skinned mesh -> Particle billboard sprites). Strictly zero mid-frame stalls.
    [[nodiscard]] bool CompositeOntoGpuViewport(
        const SimulationViewportCompositeRequest& request,
        SimulationViewportCompositeResult&        outResult);

    /// Offline proof/export readback helper (awaits the last composite timeline tick and copies the rendered RGBA8
    /// image into host memory). Never called during live Editor ticks.
    [[nodiscard]] bool ReadbackLastCompositeRgba8(
        std::vector<uint8_t>& outRgba8,
        uint32_t&             outWidthPx,
        uint32_t&             outHeightPx);

    [[nodiscard]] bool QueryReady() const noexcept { return Ready_; }
    [[nodiscard]] bool QueryFluidPipelineReady() const noexcept { return FluidPipelineReady_; }
    [[nodiscard]] bool QueryParticlePipelineReady() const noexcept { return ParticlePipelineReady_; }
    [[nodiscard]] bool QueryDeformablePipelineReady() const noexcept { return DeformablePipelineReady_; }

private:
    bool Ready_                  = false;
    bool FluidPipelineReady_     = false;
    bool ParticlePipelineReady_  = false;
    bool DeformablePipelineReady_ = false;
    uint32_t TargetWidth_        = 0u;
    uint32_t TargetHeight_       = 0u;
    uint64_t ImageGeneration_    = 0ull;
    uint32_t ActiveSlot_         = 0u;
    uint32_t DeformableIndexCount_ = 0u;
    uint32_t DeformableVertexCount_ = 0u;
    uint32_t DeformableEmbedMode_ = 1u;

#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    struct GraphicsPipelineUnit
    {
        VkShaderModule        VertModule     = VK_NULL_HANDLE;
        VkShaderModule        FragModule     = VK_NULL_HANDLE;
        VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;
        VkPipelineLayout      PipeLayout     = VK_NULL_HANDLE;
        VkPipeline            Pipeline       = VK_NULL_HANDLE;
    };

    struct PendingCmdSlot
    {
        VkCommandBuffer CmdBuffer      = VK_NULL_HANDLE;
        uint64_t        TimelineSignal = 0ull;
    };

    void RetireCompletedCommandBuffersNonBlocking() noexcept;
    [[nodiscard]] bool EnsureRenderTargets(uint32_t widthPx, uint32_t heightPx);
    void ReleaseRenderTargets() noexcept;
    [[nodiscard]] bool BuildBlitPipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv);
    [[nodiscard]] bool BuildFluidPipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv);
    [[nodiscard]] bool BuildParticlePipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv);
    [[nodiscard]] bool BuildDeformablePipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv);
    void DestroyPipelineUnit(GraphicsPipelineUnit& unit) noexcept;

    GeometricRaster::VulkanInterchange*   Interchange_     = nullptr;
    VkRenderPass                          RenderPass_      = VK_NULL_HANDLE;
    VkDescriptorPool                      DescriptorPool_  = VK_NULL_HANDLE;
    VkSampler                             LinearSampler_   = VK_NULL_HANDLE;

    GraphicsPipelineUnit                  BlitPipe_        {};
    GraphicsPipelineUnit                  FluidPipe_       {};
    GraphicsPipelineUnit                  ParticlePipe_    {};
    GraphicsPipelineUnit                  DeformablePipe_  {};

    GeometricRaster::DeviceImageResource  ColorTarget_     {};
    GeometricRaster::DeviceImageResource  DepthTarget_     {};
    VkFramebuffer                         Framebuffer_     = VK_NULL_HANDLE;

    std::array<GeometricRaster::DeviceBufferResource, kCommandRingSlots> FluidUbo_       {};
    std::array<GeometricRaster::DeviceBufferResource, kCommandRingSlots> ParticleUbo_    {};
    std::array<GeometricRaster::DeviceBufferResource, kCommandRingSlots> DeformVatUbo_   {};
    std::array<GeometricRaster::DeviceBufferResource, kCommandRingSlots> DeformCamUbo_   {};
    std::array<VkDescriptorSet, kCommandRingSlots>                       BlitDescSets_   {};
    std::array<VkDescriptorSet, kCommandRingSlots>                       FluidDescSets_  {};
    std::array<VkDescriptorSet, kCommandRingSlots>                       PartDescSets_   {};
    std::array<VkDescriptorSet, kCommandRingSlots>                       DeformDescSets_ {};

    GeometricRaster::DeviceBufferResource DeformBoundVerts_ {};
    GeometricRaster::DeviceBufferResource DeformIndices_    {};
    GeometricRaster::DeviceBufferResource FallbackParticleBuf_ {};

    std::array<PendingCmdSlot, kCommandRingSlots> InFlightCommands_ {};
    std::vector<PendingCmdSlot>                   OverflowPending_  {};
    uint64_t                                      LastSubmittedTimeline_ = 0ull;
#endif
};

} // namespace Frontier::SimulationSpace
