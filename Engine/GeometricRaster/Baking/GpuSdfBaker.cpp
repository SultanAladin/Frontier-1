/*============================================================================================================================================
                                                               GPUSDFBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of Pure-GPU Vulkan 3D Volume SDF Baker, 2D slice section extraction, PNG & .fvol export

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/GpuSdfBaker.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>

namespace Frontier::GeometricRaster::Baking
{

namespace
{

[[nodiscard]] std::filesystem::path ResolveSdfShaderDirectory(const std::filesystem::path& ExplicitDir)
{
    if (!ExplicitDir.empty() && std::filesystem::exists(ExplicitDir / "BakeVolumeSdfCompute_GTX.comp.spv"))
    {
        return ExplicitDir;
    }
    const std::array<std::filesystem::path, 7u> Candidates {
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR) / "Baking",
#endif
        std::filesystem::current_path() / "CompiledShaders",
        std::filesystem::current_path() / "Build/Editor/CompiledShaders",
        std::filesystem::current_path() / "Build/Development/CompiledShaders",
#ifdef FRONTIER_REPO_ROOT_DIR
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Build/Editor/CompiledShaders",
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Build/Development/CompiledShaders",
#endif
    };
    for (const auto& Candidate : Candidates)
    {
        if (std::filesystem::exists(Candidate / "BakeVolumeSdfCompute_GTX.comp.spv"))
        {
            return Candidate;
        }
    }
    return ExplicitDir;
}

[[nodiscard]] VkDescriptorSetLayout CreateSetLayout(
    VkDevice                                         Device,
    const std::vector<VkDescriptorSetLayoutBinding>& Bindings)
{
    VkDescriptorSetLayoutCreateInfo Spec {};
    Spec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    Spec.bindingCount = static_cast<uint32_t>(Bindings.size());
    Spec.pBindings    = Bindings.data();
    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(Device, &Spec, nullptr, &Layout) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return Layout;
}

[[nodiscard]] VkPipelineLayout CreateProgramLayout(VkDevice Device, VkDescriptorSetLayout SetLayout)
{
    VkPipelineLayoutCreateInfo Spec {};
    Spec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    Spec.setLayoutCount = 1u;
    Spec.pSetLayouts    = &SetLayout;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(Device, &Spec, nullptr, &Layout) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return Layout;
}

[[nodiscard]] VkPipeline CreateComputeProgram(
    VkDevice                     Device,
    VkPipelineLayout             Layout,
    const VulkanInterchange&     Interchange,
    const std::filesystem::path& SpvPath)
{
    VkShaderModule ShaderUnit = Interchange.LoadShaderBinary(SpvPath);
    if (ShaderUnit == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo EntrySpec {};
    EntrySpec.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    EntrySpec.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    EntrySpec.module = ShaderUnit;
    EntrySpec.pName  = "ComputeEntry";

    VkComputePipelineCreateInfo CompSpec {};
    CompSpec.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    CompSpec.stage  = EntrySpec;
    CompSpec.layout = Layout;

    VkPipeline Program = VK_NULL_HANDLE;
    const VkResult Status = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1u, &CompSpec, nullptr, &Program);
    vkDestroyShaderModule(Device, ShaderUnit, nullptr);
    return (Status == VK_SUCCESS) ? Program : VK_NULL_HANDLE;
}

[[nodiscard]] VkDescriptorSet AllocateSet(VkDevice Device, VkDescriptorPool Registry, VkDescriptorSetLayout Layout)
{
    VkDescriptorSetAllocateInfo Spec {};
    Spec.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    Spec.descriptorPool     = Registry;
    Spec.descriptorSetCount = 1u;
    Spec.pSetLayouts        = &Layout;
    VkDescriptorSet Set     = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(Device, &Spec, &Set) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return Set;
}

void AttachBufferSlot(
    std::vector<VkWriteDescriptorSet>&  OutWrites,
    std::deque<VkDescriptorBufferInfo>& StorageList,
    VkDescriptorSet                     DstSet,
    uint32_t                            Slot,
    VkDescriptorType                    Type,
    VkBuffer                            BufHandle)
{
    VkDescriptorBufferInfo BufDesc {};
    BufDesc.buffer = BufHandle;
    BufDesc.offset = 0ull;
    BufDesc.range  = VK_WHOLE_SIZE;
    StorageList.push_back(BufDesc);

    VkWriteDescriptorSet W {};
    W.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    W.dstSet          = DstSet;
    W.dstBinding      = Slot;
    W.descriptorCount = 1u;
    W.descriptorType  = Type;
    W.pBufferInfo     = &StorageList.back();
    OutWrites.push_back(W);
}

void AttachImageSlot(
    std::vector<VkWriteDescriptorSet>& OutWrites,
    std::deque<VkDescriptorImageInfo>& StorageList,
    VkDescriptorSet                    DstSet,
    uint32_t                           Slot,
    VkDescriptorType                   Type,
    VkImageView                        ViewHandle,
    VkImageLayout                      Layout)
{
    VkDescriptorImageInfo ImgDesc {};
    ImgDesc.imageView   = ViewHandle;
    ImgDesc.imageLayout = Layout;
    StorageList.push_back(ImgDesc);

    VkWriteDescriptorSet W {};
    W.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    W.dstSet          = DstSet;
    W.dstBinding      = Slot;
    W.descriptorCount = 1u;
    W.descriptorType  = Type;
    W.pImageInfo      = &StorageList.back();
    OutWrites.push_back(W);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

GpuSdfBaker::~GpuSdfBaker()
{
    TeardownSdfBaker();
}

bool GpuSdfBaker::InitializeSdfBaker(
    VulkanInterchange*           BoundDevice,
    const std::filesystem::path& ShaderDirectory)
{
    TeardownSdfBaker();
    DeviceOwner = BoundDevice;
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return false;
    }

    const std::filesystem::path ShaderDir = ResolveSdfShaderDirectory(ShaderDirectory);
    if (!CreateShaderPrograms(ShaderDir))
    {
        TeardownSdfBaker();
        return false;
    }

    VolumeUniformBuffer = DeviceOwner->AllocateBuffer(
        sizeof(GpuVolumeBakeConfig),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (VolumeUniformBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownSdfBaker();
        return false;
    }

    Initialized = true;
    return true;
}

void GpuSdfBaker::TeardownSdfBaker() noexcept
{
    ReleaseVolumeResources();

    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        vkDeviceWaitIdle(DeviceOwner->AccessDevice());
        DeviceOwner->ReleaseBuffer(VolumeUniformBuffer);
        DestroyShaderPrograms();
    }

    DeviceOwner       = nullptr;
    Initialized       = false;
    RtxProgramsLoaded = false;
}

bool GpuSdfBaker::CreateShaderPrograms(const std::filesystem::path& ShaderDir)
{
    VkDevice Device = DeviceOwner->AccessDevice();

    std::vector<VkDescriptorPoolSize> PoolSizes {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8u  },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16u },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  16u },
    };
    if (DeviceOwner->QueryHardwareRayQuerySupported())
    {
        PoolSizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4u });
    }

    VkDescriptorPoolCreateInfo PoolSpec {};
    PoolSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolSpec.maxSets       = 8u;
    PoolSpec.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    PoolSpec.pPoolSizes    = PoolSizes.data();
    if (vkCreateDescriptorPool(Device, &PoolSpec, nullptr, &DescriptorRegistry) != VK_SUCCESS)
    {
        return false;
    }

    VolumeSdfGtxSetLayout = CreateSetLayout(Device, {
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    });
    VolumeSdfGtxProgramLayout = CreateProgramLayout(Device, VolumeSdfGtxSetLayout);
    VolumeSdfGtxDescriptorSet = AllocateSet(Device, DescriptorRegistry, VolumeSdfGtxSetLayout);
    VolumeSdfGtxProgram       = CreateComputeProgram(
        Device,
        VolumeSdfGtxProgramLayout,
        *DeviceOwner,
        ShaderDir / "BakeVolumeSdfCompute_GTX.comp.spv");
    if (VolumeSdfGtxProgram == VK_NULL_HANDLE)
    {
        return false;
    }

    SectionExtractSetLayout = CreateSetLayout(Device, {
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    });
    SectionExtractProgramLayout = CreateProgramLayout(Device, SectionExtractSetLayout);
    SectionExtractDescriptorSet = AllocateSet(Device, DescriptorRegistry, SectionExtractSetLayout);
    SectionExtractProgram       = CreateComputeProgram(
        Device,
        SectionExtractProgramLayout,
        *DeviceOwner,
        ShaderDir / "ExtractSdfSectionCompute.comp.spv");
    if (SectionExtractProgram == VK_NULL_HANDLE)
    {
        return false;
    }

    RtxProgramsLoaded = false;
    if (DeviceOwner->QueryHardwareRayQuerySupported() &&
        std::filesystem::exists(ShaderDir / "BakeVolumeSdfCompute_RTX.comp.spv"))
    {
        VolumeSdfRtxSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,               1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        });
        VolumeSdfRtxProgramLayout = CreateProgramLayout(Device, VolumeSdfRtxSetLayout);
        VolumeSdfRtxDescriptorSet = AllocateSet(Device, DescriptorRegistry, VolumeSdfRtxSetLayout);
        VolumeSdfRtxProgram       = CreateComputeProgram(
            Device,
            VolumeSdfRtxProgramLayout,
            *DeviceOwner,
            ShaderDir / "BakeVolumeSdfCompute_RTX.comp.spv");

        RtxProgramsLoaded = (VolumeSdfRtxProgram != VK_NULL_HANDLE);
    }

    return true;
}

void GpuSdfBaker::DestroyShaderPrograms() noexcept
{
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return;
    }
    VkDevice Device = DeviceOwner->AccessDevice();

    const auto DestroyProg = [&](VkPipeline& P, VkPipelineLayout& PL, VkDescriptorSetLayout& SL)
    {
        if (P != VK_NULL_HANDLE)  { vkDestroyPipeline(Device, P, nullptr); P = VK_NULL_HANDLE; }
        if (PL != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Device, PL, nullptr); PL = VK_NULL_HANDLE; }
        if (SL != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, SL, nullptr); SL = VK_NULL_HANDLE; }
    };

    DestroyProg(VolumeSdfGtxProgram, VolumeSdfGtxProgramLayout, VolumeSdfGtxSetLayout);
    DestroyProg(VolumeSdfRtxProgram, VolumeSdfRtxProgramLayout, VolumeSdfRtxSetLayout);
    DestroyProg(SectionExtractProgram, SectionExtractProgramLayout, SectionExtractSetLayout);

    if (DescriptorRegistry != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device, DescriptorRegistry, nullptr);
        DescriptorRegistry = VK_NULL_HANDLE;
    }
}

bool GpuSdfBaker::EnsureVolumeResources(uint32_t GridDim, uint32_t SectionSizePx)
{
    if (AllocatedVolumeDim == GridDim && AllocatedSectionPx == SectionSizePx && VolumeDistanceBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        return true;
    }
    ReleaseVolumeResources();

    const uint64_t VoxelCount = static_cast<uint64_t>(GridDim) * GridDim * GridDim;
    VolumeDistanceBuffer = DeviceOwner->AllocateBuffer(
        VoxelCount * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VolumeTexture3D = DeviceOwner->AllocateImage3D(
        GridDim,
        GridDim,
        GridDim,
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    SectionRgba8Image = DeviceOwner->AllocateImage2D(
        SectionSizePx,
        SectionSizePx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    SectionReadbackBuffer = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(SectionSizePx) * SectionSizePx * 4ull,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (VolumeDistanceBuffer.BufferHandle == VK_NULL_HANDLE ||
        VolumeTexture3D.ImageHandle == VK_NULL_HANDLE ||
        SectionRgba8Image.ImageHandle == VK_NULL_HANDLE ||
        SectionReadbackBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        ReleaseVolumeResources();
        return false;
    }

    AllocatedVolumeDim = GridDim;
    AllocatedSectionPx = SectionSizePx;
    return true;
}

void GpuSdfBaker::ReleaseVolumeResources() noexcept
{
    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        vkDeviceWaitIdle(DeviceOwner->AccessDevice());
        DeviceOwner->ReleaseBuffer(VolumeDistanceBuffer);
        DeviceOwner->ReleaseBuffer(SectionReadbackBuffer);
        DeviceOwner->ReleaseImage2D(VolumeTexture3D);
        DeviceOwner->ReleaseImage2D(SectionRgba8Image);
    }
    AllocatedVolumeDim = 0u;
    AllocatedSectionPx = 0u;
}

void GpuSdfBaker::TransitionImageLayout(
    VkCommandBuffer      Cmd,
    VkImage              ImageHandle,
    VkImageLayout        OldLayout,
    VkImageLayout        NewLayout,
    uint32_t             LayerCount,
    VkPipelineStageFlags SrcStageMask,
    VkPipelineStageFlags DstStageMask) const noexcept
{
    VkImageMemoryBarrier Barrier {};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = OldLayout;
    Barrier.newLayout                       = NewLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = ImageHandle;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = LayerCount;
    Barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    Barrier.dstAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(Cmd, SrcStageMask, DstStageMask, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             VOLUME SDF BAKING & SECTION
//------------------------------------------------------------------------------------------------------------------------

BakeCompletionSummary GpuSdfBaker::ExecuteVolumeSdfBake(
    const BakingAccelerationStructure& Accel,
    const PreparedStudyPair&           Study,
    const VolumeBakeExecutionSpec&     Spec)
{
    BakeCompletionSummary Summary {};
    if (!Initialized || !Accel.Ready())
    {
        Summary.DiagnosticText = "SDF baker or study acceleration structure uninitialized.";
        return Summary;
    }

    const uint32_t Dim       = std::clamp(Spec.GridDim, 16u, 128u);
    const uint32_t SectionPx = std::clamp(Spec.SectionSizePx, 128u, 1024u);
    if (!EnsureVolumeResources(Dim, SectionPx))
    {
        Summary.DiagnosticText = "Failed allocating GPU 3D volume SDF buffers.";
        return Summary;
    }

    const auto T0 = std::chrono::steady_clock::now();
    const bool UseRtx = (!Spec.PreferComputeBvh4 && Accel.QueryHardwareAccelReady() && RtxProgramsLoaded);
    Summary.ExecutedBackend = UseRtx
        ? BakeAccelerationBackend::RtxHardwareRayQuery
        : BakeAccelerationBackend::GtxComputeBvh4;
    LastStudyDisplayLabel = Study.StudyDisplayLabel;

    const Mathematics::Axis3D Span      = Study.BoundsMax - Study.BoundsMin;
    const float               MaxSpan   = std::max({ 0.05f, Span.X, Span.Y, Span.Z });
    const float               Pad       = std::clamp(Spec.BoundsPaddingFraction, 0.02f, 0.80f) * MaxSpan;
    const Mathematics::Axis3D MinP      { Study.BoundsMin.X - Pad, Study.BoundsMin.Y - Pad, Study.BoundsMin.Z - Pad };
    const Mathematics::Axis3D MaxP      { Study.BoundsMax.X + Pad, Study.BoundsMax.Y + Pad, Study.BoundsMax.Z + Pad };
    const float               CubicSpan = std::max({ MaxP.X - MinP.X, MaxP.Y - MinP.Y, MaxP.Z - MinP.Z });
    const Mathematics::Axis3D Center    {
        0.5f * (MinP.X + MaxP.X),
        0.5f * (MinP.Y + MaxP.Y),
        0.5f * (MinP.Z + MaxP.Z),
    };
    const Mathematics::Axis3D CubicMin  { Center.X - CubicSpan * 0.5f, Center.Y - CubicSpan * 0.5f, Center.Z - CubicSpan * 0.5f };
    const Mathematics::Axis3D CubicMax  { Center.X + CubicSpan * 0.5f, Center.Y + CubicSpan * 0.5f, Center.Z + CubicSpan * 0.5f };

    GpuVolumeBakeConfig Cfg {};
    Cfg.GridDim                = Dim;
    Cfg.HighTriangleCount      = Accel.QueryHighTriangleCount();
    Cfg.WideEntryCount         = Accel.QueryWideEntryCount();
    Cfg.SignedMode             = Spec.SignedInteriorDistance ? 1u : 0u;
    Cfg.BoundsMin[0]           = CubicMin.X;
    Cfg.BoundsMin[1]           = CubicMin.Y;
    Cfg.BoundsMin[2]           = CubicMin.Z;
    Cfg.VoxelStepMeters        = CubicSpan / static_cast<float>(Dim);
    Cfg.BoundsMax[0]           = CubicMax.X;
    Cfg.BoundsMax[1]           = CubicMax.Y;
    Cfg.BoundsMax[2]           = CubicMax.Z;
    Cfg.BoundsDiagMeters       = CubicSpan * 1.7320508f;
    Cfg.SectionAxis            = std::min(Spec.SectionAxis, 2u);
    Cfg.SectionDepthNormalized = std::clamp(Spec.SectionDepthNormalized, 0.0f, 1.0f);
    Cfg.SectionSizePx          = SectionPx;
    LastVolumeConfig           = Cfg;
    std::memcpy(VolumeUniformBuffer.HostAddress, &Cfg, sizeof(Cfg));

    VkDevice Device = DeviceOwner->AccessDevice();
    std::vector<VkWriteDescriptorSet>  Writes;
    std::deque<VkDescriptorBufferInfo> BufSlots;
    std::deque<VkDescriptorImageInfo>  ImgSlots;

    VkWriteDescriptorSetAccelerationStructureKHR AccelWrite {};
    VkAccelerationStructureKHR TlasHandle = Accel.AccessTopLevelAccel();
    if (UseRtx)
    {
        AttachBufferSlot(Writes, BufSlots, VolumeSdfRtxDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VolumeUniformBuffer.BufferHandle);
        AttachBufferSlot(Writes, BufSlots, VolumeSdfRtxDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessWideEntryBuffer());
        AttachBufferSlot(Writes, BufSlots, VolumeSdfRtxDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessBvhPrimitiveBuffer());
        AttachBufferSlot(Writes, BufSlots, VolumeSdfRtxDescriptorSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VolumeDistanceBuffer.BufferHandle);
        AttachImageSlot (Writes, ImgSlots, VolumeSdfRtxDescriptorSet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VolumeTexture3D.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AccelWrite.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        AccelWrite.accelerationStructureCount = 1u;
        AccelWrite.pAccelerationStructures    = &TlasHandle;
        VkWriteDescriptorSet AccW {};
        AccW.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        AccW.pNext           = &AccelWrite;
        AccW.dstSet          = VolumeSdfRtxDescriptorSet;
        AccW.dstBinding      = 5u;
        AccW.descriptorCount = 1u;
        AccW.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        Writes.push_back(AccW);
    }
    else
    {
        AttachBufferSlot(Writes, BufSlots, VolumeSdfGtxDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VolumeUniformBuffer.BufferHandle);
        AttachBufferSlot(Writes, BufSlots, VolumeSdfGtxDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessWideEntryBuffer());
        AttachBufferSlot(Writes, BufSlots, VolumeSdfGtxDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessBvhPrimitiveBuffer());
        AttachBufferSlot(Writes, BufSlots, VolumeSdfGtxDescriptorSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VolumeDistanceBuffer.BufferHandle);
        AttachImageSlot (Writes, ImgSlots, VolumeSdfGtxDescriptorSet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VolumeTexture3D.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
    }

    AttachBufferSlot(Writes, BufSlots, SectionExtractDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VolumeUniformBuffer.BufferHandle);
    AttachBufferSlot(Writes, BufSlots, SectionExtractDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VolumeDistanceBuffer.BufferHandle);
    AttachImageSlot (Writes, ImgSlots, SectionExtractDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SectionRgba8Image.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);

    vkUpdateDescriptorSets(Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    VkCommandBuffer Cmd = DeviceOwner->BeginSingleTimeCommands();
    TransitionImageLayout(Cmd, VolumeTexture3D.ImageHandle,   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);
    TransitionImageLayout(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);

    const uint32_t VolGroups = (Dim + 3u) / 4u;
    if (UseRtx)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, VolumeSdfRtxProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, VolumeSdfRtxProgramLayout, 0u, 1u, &VolumeSdfRtxDescriptorSet, 0u, nullptr);
    }
    else
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, VolumeSdfGtxProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, VolumeSdfGtxProgramLayout, 0u, 1u, &VolumeSdfGtxDescriptorSet, 0u, nullptr);
    }
    vkCmdDispatch(Cmd, VolGroups, VolGroups, VolGroups);

    VkMemoryBarrier MemBarrier {};
    MemBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    MemBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    MemBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &MemBarrier, 0u, nullptr, 0u, nullptr);

    const uint32_t SecGroups = (SectionPx + 7u) / 8u;
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SectionExtractProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SectionExtractProgramLayout, 0u, 1u, &SectionExtractDescriptorSet, 0u, nullptr);
    vkCmdDispatch(Cmd, SecGroups, SecGroups, 1u);

    TransitionImageLayout(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1u);
    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { SectionPx, SectionPx, 1u };
    vkCmdCopyImageToBuffer(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SectionReadbackBuffer.BufferHandle, 1u, &CopyRegion);

    const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(Cmd);
    if (!DeviceOwner->AwaitTimelineTick(Tick, 60000000000ull))
    {
        vkQueueWaitIdle(DeviceOwner->AccessQueue());
        DeviceOwner->FreeCommandBuffer(Cmd);
        Summary.DiagnosticText = "GPU volume SDF bake timed out.";
        return Summary;
    }
    DeviceOwner->FreeCommandBuffer(Cmd);

    const std::size_t VoxelCount = static_cast<std::size_t>(Dim) * Dim * Dim;
    CachedVolumeDistances.resize(VoxelCount);
    if (VolumeDistanceBuffer.HostAddress != nullptr)
    {
        std::memcpy(CachedVolumeDistances.data(), VolumeDistanceBuffer.HostAddress, VoxelCount * sizeof(float));
    }

    const std::size_t SecBytes = static_cast<std::size_t>(SectionPx) * SectionPx * 4u;
    CachedSectionRgba8.resize(SecBytes);
    if (SectionReadbackBuffer.HostAddress != nullptr)
    {
        std::memcpy(CachedSectionRgba8.data(), SectionReadbackBuffer.HostAddress, SecBytes);
    }

    const auto T1 = std::chrono::steady_clock::now();
    Summary.VolumeSucceeded = true;
    Summary.VolumeElapsedMs = std::chrono::duration<double, std::milli>(T1 - T0).count();
    Summary.DiagnosticText  = UseRtx
        ? "RTX RayQuery 3D SDF Bake Complete"
        : "GTX BVH4 Compute 3D SDF Bake Complete";
    return Summary;
}

bool GpuSdfBaker::UpdateSdfSectionPreview(
    uint32_t SectionAxis,
    float    SectionDepthNormalized,
    bool     SignedInteriorDistance,
    uint32_t SectionSizePx)
{
    if (!Initialized || VolumeDistanceBuffer.BufferHandle == VK_NULL_HANDLE || AllocatedVolumeDim == 0u)
    {
        return false;
    }
    const uint32_t TargetPx = std::clamp(SectionSizePx, 128u, 1024u);
    if (!EnsureVolumeResources(AllocatedVolumeDim, TargetPx))
    {
        return false;
    }

    LastVolumeConfig.SectionAxis            = std::min(SectionAxis, 2u);
    LastVolumeConfig.SectionDepthNormalized = std::clamp(SectionDepthNormalized, 0.0f, 1.0f);
    LastVolumeConfig.SignedMode             = SignedInteriorDistance ? 1u : 0u;
    LastVolumeConfig.SectionSizePx          = TargetPx;
    std::memcpy(VolumeUniformBuffer.HostAddress, &LastVolumeConfig, sizeof(LastVolumeConfig));

    std::vector<VkWriteDescriptorSet>  Writes;
    std::deque<VkDescriptorBufferInfo> BufSlots;
    std::deque<VkDescriptorImageInfo>  ImgSlots;
    AttachBufferSlot(Writes, BufSlots, SectionExtractDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VolumeUniformBuffer.BufferHandle);
    AttachBufferSlot(Writes, BufSlots, SectionExtractDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VolumeDistanceBuffer.BufferHandle);
    AttachImageSlot (Writes, ImgSlots, SectionExtractDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SectionRgba8Image.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
    vkUpdateDescriptorSets(DeviceOwner->AccessDevice(), static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    VkCommandBuffer Cmd = DeviceOwner->BeginSingleTimeCommands();
    TransitionImageLayout(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);

    const uint32_t Groups = (TargetPx + 7u) / 8u;
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SectionExtractProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SectionExtractProgramLayout, 0u, 1u, &SectionExtractDescriptorSet, 0u, nullptr);
    vkCmdDispatch(Cmd, Groups, Groups, 1u);

    TransitionImageLayout(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1u);
    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { TargetPx, TargetPx, 1u };
    vkCmdCopyImageToBuffer(Cmd, SectionRgba8Image.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SectionReadbackBuffer.BufferHandle, 1u, &CopyRegion);

    const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(Cmd);
    const bool Ok       = DeviceOwner->AwaitTimelineTick(Tick);
    DeviceOwner->FreeCommandBuffer(Cmd);
    if (!Ok)
    {
        return false;
    }

    const std::size_t SecBytes = static_cast<std::size_t>(TargetPx) * TargetPx * 4u;
    CachedSectionRgba8.resize(SecBytes);
    if (SectionReadbackBuffer.HostAddress != nullptr)
    {
        std::memcpy(CachedSectionRgba8.data(), SectionReadbackBuffer.HostAddress, SecBytes);
    }
    return true;
}

bool GpuSdfBaker::ExportSdfSectionPng(const std::filesystem::path& OutputPngPath) const
{
    if (AllocatedSectionPx == 0u)
    {
        return false;
    }
    const std::size_t Expected = static_cast<std::size_t>(AllocatedSectionPx) * AllocatedSectionPx * 4u;
    if (CachedSectionRgba8.size() < Expected)
    {
        return false;
    }
    std::error_code Ec;
    if (OutputPngPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputPngPath.parent_path(), Ec);
    }
    return stbi_write_png(
               OutputPngPath.string().c_str(),
               static_cast<int>(AllocatedSectionPx),
               static_cast<int>(AllocatedSectionPx),
               4,
               CachedSectionRgba8.data(),
               static_cast<int>(AllocatedSectionPx * 4u)) != 0;
}

bool GpuSdfBaker::ExportVolumeFvol(const std::filesystem::path& OutputFvolPath) const
{
    if (AllocatedVolumeDim == 0u || CachedVolumeDistances.empty())
    {
        return false;
    }
    std::error_code Ec;
    if (OutputFvolPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputFvolPath.parent_path(), Ec);
    }
    std::ofstream Out(OutputFvolPath);
    if (!Out.is_open())
    {
        return false;
    }
    Out << "{\n"
        << "  \"format\": \"frontier.fvol.v1\",\n"
        << "  \"study\": \"" << LastStudyDisplayLabel << "\",\n"
        << "  \"dimensions\": [" << AllocatedVolumeDim << ", " << AllocatedVolumeDim << ", " << AllocatedVolumeDim << "],\n"
        << "  \"boundsMin\": [" << LastVolumeConfig.BoundsMin[0] << ", " << LastVolumeConfig.BoundsMin[1] << ", " << LastVolumeConfig.BoundsMin[2] << "],\n"
        << "  \"boundsMax\": [" << LastVolumeConfig.BoundsMax[0] << ", " << LastVolumeConfig.BoundsMax[1] << ", " << LastVolumeConfig.BoundsMax[2] << "],\n"
        << "  \"voxelStepMeters\": " << LastVolumeConfig.VoxelStepMeters << ",\n"
        << "  \"signedInterior\": " << (LastVolumeConfig.SignedMode != 0u ? "true" : "false") << ",\n"
        << "  \"distances\": [";
    for (std::size_t I = 0u; I < CachedVolumeDistances.size(); ++I)
    {
        if (I > 0u) Out << ((I % 12u == 0u) ? ",\n    " : ",");
        Out << CachedVolumeDistances[I];
    }
    Out << "]\n}\n";
    return Out.good();
}

} // namespace Frontier::GeometricRaster::Baking
