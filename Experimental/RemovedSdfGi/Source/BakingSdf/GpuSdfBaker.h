/*============================================================================================================================================
                                                                GPUSDFBAKER.H
============================================================================================================================================*/
// 🧩 Pure-GPU Vulkan 3D Volume Signed Distance Field baker, 2D slice section extractor, PNG & .fvol exporter (GTX BVH4 + RTX RayQuery)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"
#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"
#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct VolumeBakeExecutionSpec
{
    uint32_t GridDim                 = 36u;
    bool     SignedInteriorDistance  = true;
    float    BoundsPaddingFraction   = 0.16f;
    uint32_t SectionAxis             = 0u;
    float    SectionDepthNormalized  = 0.50f;
    uint32_t SectionSizePx           = 512u;
    bool     PreferComputeBvh4       = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class GpuSdfBaker
{
public:
    GpuSdfBaker() = default;
    ~GpuSdfBaker();

    GpuSdfBaker(const GpuSdfBaker&)            = delete;
    GpuSdfBaker& operator=(const GpuSdfBaker&) = delete;

    [[nodiscard]] bool InitializeSdfBaker(
        VulkanInterchange*           BoundDevice,
        const std::filesystem::path& ShaderDirectory = {});

    void TeardownSdfBaker() noexcept;

    [[nodiscard]] BakeCompletionSummary ExecuteVolumeSdfBake(
        const BakingAccelerationStructure& Accel,
        const PreparedStudyPair&           Study,
        const VolumeBakeExecutionSpec&     Spec);

    [[nodiscard]] bool UpdateSdfSectionPreview(
        uint32_t SectionAxis,
        float    SectionDepthNormalized,
        bool     SignedInteriorDistance,
        uint32_t SectionSizePx = 512u);

    [[nodiscard]] bool ExportSdfSectionPng(const std::filesystem::path& OutputPngPath) const;
    [[nodiscard]] bool ExportVolumeFvol(const std::filesystem::path& OutputFvolPath) const;

    [[nodiscard]] bool Ready() const noexcept { return Initialized; }
    [[nodiscard]] bool QueryRtxShadersLoaded() const noexcept { return RtxProgramsLoaded; }
    [[nodiscard]] uint32_t QueryVolumeGridDim() const noexcept { return AllocatedVolumeDim; }
    [[nodiscard]] uint32_t QuerySdfSectionSizePx() const noexcept { return AllocatedSectionPx; }
    [[nodiscard]] const std::vector<uint8_t>& InspectSdfSectionRgba8() const noexcept { return CachedSectionRgba8; }
    [[nodiscard]] const std::vector<float>&   InspectVolumeDistances() const noexcept { return CachedVolumeDistances; }

private:
    [[nodiscard]] bool CreateShaderPrograms(const std::filesystem::path& ShaderDir);
    void               DestroyShaderPrograms() noexcept;
    [[nodiscard]] bool EnsureVolumeResources(uint32_t GridDim, uint32_t SectionSizePx);
    void               ReleaseVolumeResources() noexcept;
    void               TransitionImageLayout(
        VkCommandBuffer      Cmd,
        VkImage              ImageHandle,
        VkImageLayout        OldLayout,
        VkImageLayout        NewLayout,
        uint32_t             LayerCount,
        VkPipelineStageFlags SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VkPipelineStageFlags DstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) const noexcept;

    VulkanInterchange*    DeviceOwner                 = nullptr;
    bool                  Initialized                 = false;
    bool                  RtxProgramsLoaded           = false;

    VkDescriptorPool      DescriptorRegistry          = VK_NULL_HANDLE;

    VkDescriptorSetLayout VolumeSdfGtxSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout      VolumeSdfGtxProgramLayout   = VK_NULL_HANDLE;
    VkPipeline            VolumeSdfGtxProgram         = VK_NULL_HANDLE;
    VkDescriptorSet       VolumeSdfGtxDescriptorSet   = VK_NULL_HANDLE;

    VkDescriptorSetLayout VolumeSdfRtxSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout      VolumeSdfRtxProgramLayout   = VK_NULL_HANDLE;
    VkPipeline            VolumeSdfRtxProgram         = VK_NULL_HANDLE;
    VkDescriptorSet       VolumeSdfRtxDescriptorSet   = VK_NULL_HANDLE;

    VkDescriptorSetLayout SectionExtractSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout      SectionExtractProgramLayout = VK_NULL_HANDLE;
    VkPipeline            SectionExtractProgram       = VK_NULL_HANDLE;
    VkDescriptorSet       SectionExtractDescriptorSet = VK_NULL_HANDLE;

    DeviceBufferResource  VolumeUniformBuffer         {};
    DeviceBufferResource  VolumeDistanceBuffer        {};
    DeviceImageResource   VolumeTexture3D             {};
    DeviceImageResource   SectionRgba8Image           {};
    DeviceBufferResource  SectionReadbackBuffer       {};

    uint32_t              AllocatedVolumeDim          = 0u;
    uint32_t              AllocatedSectionPx          = 0u;
    GpuVolumeBakeConfig   LastVolumeConfig            {};
    std::string           LastStudyDisplayLabel       {};

    std::vector<uint8_t>  CachedSectionRgba8          {};
    std::vector<float>    CachedVolumeDistances       {};
};

} // namespace Frontier::GeometricRaster::Baking
