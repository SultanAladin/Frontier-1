/*============================================================================================================================================
                                                              GPUTEXTUREBAKER.H
============================================================================================================================================*/
// 🧩 Pure-GPU Vulkan 17-channel surface texture baking engine (UV G-buffer, GTX BVH4 compute, RTX VK_KHR_ray_query, & gutter dilation)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"
#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceBakeExecutionSpec
{
    uint32_t ResolutionPx            = 512u;
    uint32_t AoSampleCount           = 12u;
    float    FrontCageMeters         = 0.14f;
    float    RearCageMeters          = 0.14f;
    float    AoMaxRayDistanceMeters  = 0.45f;
    float    TangentSignY            = 1.0f;
    uint32_t EdgePaddingPx           = 6u;
    uint32_t ActiveTargetBitmask     = 0xFFFFFFFFu;
    bool     PreferComputeBvh4       = false;
};

struct BakeCompletionSummary
{
    bool                    SurfaceSucceeded = false;
    double                  SurfaceElapsedMs = 0.0;
    BakeAccelerationEngine ExecutedBackend  = BakeAccelerationEngine::GtxComputeBvh4;
    std::string             DiagnosticText   {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class GpuTextureBaker
{
public:
    static constexpr uint32_t SurfaceTargetCount = 17u; // Channels 0..16 mapped directly to slots 0..16

    GpuTextureBaker() = default;
    ~GpuTextureBaker();

    GpuTextureBaker(const GpuTextureBaker&)            = delete;
    GpuTextureBaker& operator=(const GpuTextureBaker&) = delete;

    [[nodiscard]] bool InitializeBaker(
        VulkanInterchange*           BoundDevice,
        const std::filesystem::path& ShaderDirectory = {});

    void TeardownBaker() noexcept;

    [[nodiscard]] bool UploadStudyGeometry(const PreparedStudyPair& Study);

    [[nodiscard]] BakeCompletionSummary ExecuteSurfaceBake(const SurfaceBakeExecutionSpec& Spec);

    [[nodiscard]] bool ExportSurfaceChannelPng(
        uint32_t                     ChannelSlot,
        const std::filesystem::path& OutputPngPath) const;

    [[nodiscard]] bool Ready() const noexcept { return Initialized; }
    [[nodiscard]] bool QueryStudyUploaded() const noexcept { return Accel.Ready(); }
    [[nodiscard]] bool QueryHardwareRayQueryAvailable() const noexcept { return Accel.QueryHardwareAccelReady(); }
    [[nodiscard]] bool QueryRtxShadersLoaded() const noexcept { return RtxProgramsLoaded; }
    [[nodiscard]] BakeAccelerationEngine QueryActiveBackend(bool PreferComputeBvh4 = false) const noexcept
    {
        if (!PreferComputeBvh4 && Accel.QueryHardwareAccelReady() && RtxProgramsLoaded)
        {
            return BakeAccelerationEngine::RtxHardwareRayQuery;
        }
        return BakeAccelerationEngine::GtxComputeBvh4;
    }

    [[nodiscard]] uint32_t QuerySurfaceResolutionPx() const noexcept { return AllocatedSurfaceResPx; }
    [[nodiscard]] const PreparedStudyPair& InspectActiveStudy() const noexcept { return ActiveStudy; }
    [[nodiscard]] const BakingAccelerationStructure& InspectAccel() const noexcept { return Accel; }

    [[nodiscard]] VkImageView AccessBakedChannelArrayView() const noexcept
    {
        if (!SurfaceTexturesReady || AllocatedSurfaceResPx == 0u)
        {
            return DefaultArrayView;
        }
        return (ActiveChannelArrayIndex == 0u) ? ChannelArrayViewA : ChannelArrayViewB;
    }
    [[nodiscard]] VkSampler AccessLinearSampler() const noexcept { return LinearClampSampler; }

    [[nodiscard]] const std::vector<uint8_t>& InspectChannelRgba8(uint32_t ChannelSlot) const noexcept
    {
        static const std::vector<uint8_t> Empty {};
        return (ChannelSlot < SurfaceTargetCount) ? CachedSurfaceChannelsRgba8[ChannelSlot] : Empty;
    }

private:
    struct ArrayImageBundle
    {
        VkImage        ImageHandle  = VK_NULL_HANDLE;
        VkDeviceMemory MemoryHandle = VK_NULL_HANDLE;
        VkImageView    ViewHandle   = VK_NULL_HANDLE;
        uint32_t       WidthPx      = 0u;
        uint32_t       HeightPx     = 0u;
        uint32_t       LayerCount   = 0u;
    };

    [[nodiscard]] bool CreateShaderPrograms(const std::filesystem::path& ShaderDir);
    void               DestroyShaderPrograms() noexcept;
    [[nodiscard]] bool EnsureSurfaceResources(uint32_t ResolutionPx);
    void               ReleaseSurfaceResources() noexcept;
    [[nodiscard]] bool AllocateArrayImage(uint32_t ResPx, uint32_t Layers, ArrayImageBundle& OutBundle) const;
    void               ReleaseArrayImage(ArrayImageBundle& Bundle) const noexcept;
    void               TransitionImageLayout(
        VkCommandBuffer      Cmd,
        VkImage              ImageHandle,
        VkImageLayout        OldLayout,
        VkImageLayout        NewLayout,
        uint32_t             LayerCount,
        VkPipelineStageFlags SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VkPipelineStageFlags DstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) const noexcept;

    VulkanInterchange*          DeviceOwner             = nullptr;
    PreparedStudyPair           ActiveStudy             {};
    BakingAccelerationStructure Accel                   {};
    bool                        Initialized             = false;
    bool                        RtxProgramsLoaded       = false;

    VkDescriptorPool            DescriptorRegistry      = VK_NULL_HANDLE;
    VkSampler                   LinearClampSampler      = VK_NULL_HANDLE;

    VkRenderPass                UvGbufferRenderSequence = VK_NULL_HANDLE;
    VkFramebuffer               UvGbufferTarget         = VK_NULL_HANDLE;
    VkDescriptorSetLayout       UvRasterSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout            UvRasterProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                  UvRasterProgram         = VK_NULL_HANDLE;
    VkDescriptorSet             UvRasterDescriptorSet   = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SurfaceTransferGtxSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout            SurfaceTransferGtxProgramLayout = VK_NULL_HANDLE;
    VkPipeline                  SurfaceTransferGtxProgram       = VK_NULL_HANDLE;
    VkDescriptorSet             SurfaceTransferGtxDescriptorSet = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SurfaceTransferRtxSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout            SurfaceTransferRtxProgramLayout = VK_NULL_HANDLE;
    VkPipeline                  SurfaceTransferRtxProgram       = VK_NULL_HANDLE;
    VkDescriptorSet             SurfaceTransferRtxDescriptorSet = VK_NULL_HANDLE;

    VkDescriptorSetLayout       ChannelResolveSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout            ChannelResolveProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                  ChannelResolveProgram         = VK_NULL_HANDLE;
    VkDescriptorSet             ChannelResolveDescriptorSet   = VK_NULL_HANDLE;

    VkDescriptorSetLayout       DilationSetLayout             = VK_NULL_HANDLE;
    VkPipelineLayout            DilationProgramLayout         = VK_NULL_HANDLE;
    VkPipeline                  DilationProgram               = VK_NULL_HANDLE;
    VkDescriptorSet             DilationSetAtoB               = VK_NULL_HANDLE;
    VkDescriptorSet             DilationSetBtoA               = VK_NULL_HANDLE;

    DeviceBufferResource        SurfaceUniformBuffer    {};
    DeviceImageResource         LowPosMaskImage         {};
    DeviceImageResource         LowNormPartImage        {};
    DeviceImageResource         LowTangIslandImage      {};
    DeviceImageResource         LowBitanWireImage       {};
    DeviceImageResource         HighPosHeightImage      {};
    DeviceImageResource         HighNormPartImage       {};
    DeviceImageResource         OcclusionStatsImage     {};
    DeviceImageResource         BentNormalWorldImage    {};
    ArrayImageBundle            ChannelArrayA           {};
    ArrayImageBundle            ChannelArrayB           {};
    ArrayImageBundle            DefaultArrayBundle      {};
    VkImageView                 DefaultArrayView        = VK_NULL_HANDLE;
    VkImageView                 ChannelArrayViewA       = VK_NULL_HANDLE;
    VkImageView                 ChannelArrayViewB       = VK_NULL_HANDLE;
    DeviceBufferResource        ChannelReadbackBuffer   {};

    uint32_t                    AllocatedSurfaceResPx   = 0u;
    uint32_t                    ActiveChannelArrayIndex = 0u;
    bool                        SurfaceTexturesReady    = false;

    std::array<std::vector<uint8_t>, SurfaceTargetCount> CachedSurfaceChannelsRgba8 {};
};

} // namespace Frontier::GeometricRaster::Baking
