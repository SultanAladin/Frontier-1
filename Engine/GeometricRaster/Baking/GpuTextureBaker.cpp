/*============================================================================================================================================
                                                             GPUTEXTUREBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of Pure-GPU Vulkan 17-Channel Surface Texture Baker (UV G-Buffer, BVH4 GTX / RayQuery RTX, Dilation & PNG export)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"

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
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>

namespace Frontier::GeometricRaster::Baking
{

namespace
{

[[nodiscard]] std::filesystem::path ResolveBakingShaderDirectory(const std::filesystem::path& ExplicitDir)
{
    if (!ExplicitDir.empty() && std::filesystem::exists(ExplicitDir / "BakeLowUvRasterization.vert.spv"))
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
        if (std::filesystem::exists(Candidate / "BakeLowUvRasterization.vert.spv"))
        {
            return Candidate;
        }
    }
    return ExplicitDir;
}

[[nodiscard]] VkDescriptorSetLayout CreateSetLayout(
    VkDevice                                          Device,
    const std::vector<VkDescriptorSetLayoutBinding>&  Bindings)
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

[[nodiscard]] VkPipelineLayout CreateProgramLayout(
    VkDevice              Device,
    VkDescriptorSetLayout SetLayout)
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
    VkDevice                   Device,
    VkPipelineLayout           Layout,
    const VulkanInterchange&   Interchange,
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

[[nodiscard]] VkDescriptorSet AllocateSet(
    VkDevice              Device,
    VkDescriptorPool      Registry,
    VkDescriptorSetLayout Layout)
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
    std::vector<VkWriteDescriptorSet>&   OutWrites,
    std::deque<VkDescriptorBufferInfo>&  StorageList,
    VkDescriptorSet                      DstSet,
    uint32_t                             Slot,
    VkDescriptorType                     Type,
    VkBuffer                             BufHandle)
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
    std::vector<VkWriteDescriptorSet>&  OutWrites,
    std::deque<VkDescriptorImageInfo>&  StorageList,
    VkDescriptorSet                     DstSet,
    uint32_t                            Slot,
    VkDescriptorType                    Type,
    VkImageView                         ViewHandle,
    VkImageLayout                       Layout)
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

GpuTextureBaker::~GpuTextureBaker()
{
    TeardownBaker();
}

bool GpuTextureBaker::InitializeBaker(
    VulkanInterchange*           BoundDevice,
    const std::filesystem::path& ShaderDirectory)
{
    TeardownBaker();
    DeviceOwner = BoundDevice;
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return false;
    }

    const std::filesystem::path ShaderDir = ResolveBakingShaderDirectory(ShaderDirectory);
    if (!CreateShaderPrograms(ShaderDir))
    {
        TeardownBaker();
        return false;
    }

    // Allocate persistent uniform buffers
    SurfaceUniformBuffer = DeviceOwner->AllocateBuffer(
        sizeof(GpuSurfaceBakeConfig),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (SurfaceUniformBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownBaker();
        return false;
    }

    VkSamplerCreateInfo SampSpec {};
    SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SampSpec.magFilter    = VK_FILTER_LINEAR;
    SampSpec.minFilter    = VK_FILTER_LINEAR;
    SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.maxLod       = 1.0f;
    if (vkCreateSampler(DeviceOwner->AccessDevice(), &SampSpec, nullptr, &LinearClampSampler) != VK_SUCCESS)
    {
        TeardownBaker();
        return false;
    }

    if (!AllocateArrayImage(1u, SurfaceTargetCount, DefaultArrayBundle))
    {
        TeardownBaker();
        return false;
    }
    DefaultArrayView = DefaultArrayBundle.ViewHandle;
    {
        VkCommandBuffer ClearCmd = DeviceOwner->BeginSingleTimeCommands();
        TransitionImageLayout(ClearCmd, DefaultArrayBundle.ImageHandle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, SurfaceTargetCount);
        VkClearColorValue ZeroClear {{ 0.0f, 0.0f, 0.0f, 0.0f }};
        VkImageSubresourceRange ClearRange { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, SurfaceTargetCount };
        vkCmdClearColorImage(ClearCmd, DefaultArrayBundle.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ZeroClear, 1u, &ClearRange);
        TransitionImageLayout(ClearCmd, DefaultArrayBundle.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, SurfaceTargetCount);
        const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(ClearCmd);
        (void)DeviceOwner->AwaitTimelineTick(Tick);
        DeviceOwner->FreeCommandBuffer(ClearCmd);
    }

    Initialized = true;
    return true;
}

void GpuTextureBaker::TeardownBaker() noexcept
{
    Accel.ReleaseResources();
    ReleaseSurfaceResources();

    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        VkDevice Device = DeviceOwner->AccessDevice();
        vkDeviceWaitIdle(Device);

        ReleaseArrayImage(DefaultArrayBundle);
        DefaultArrayView = VK_NULL_HANDLE;

        DeviceOwner->ReleaseBuffer(SurfaceUniformBuffer);

        if (LinearClampSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Device, LinearClampSampler, nullptr);
            LinearClampSampler = VK_NULL_HANDLE;
        }
        DestroyShaderPrograms();
    }

    Initialized       = false;
    RtxProgramsLoaded = false;
    DeviceOwner       = nullptr;
}

bool GpuTextureBaker::UploadStudyGeometry(const PreparedStudyPair& Study)
{
    if (!Initialized || DeviceOwner == nullptr)
    {
        return false;
    }
    ActiveStudy = Study;
    SurfaceTexturesReady = false;
    for (auto& Vec : CachedSurfaceChannelsRgba8)
    {
        Vec.clear();
    }
    return Accel.UploadPreparedStudy(DeviceOwner, ActiveStudy);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN PROGRAM CREATION
//------------------------------------------------------------------------------------------------------------------------

bool GpuTextureBaker::CreateShaderPrograms(const std::filesystem::path& ShaderDir)
{
    VkDevice Device = DeviceOwner->AccessDevice();

    // 1. Descriptor Pool accommodating GTX, RTX, Channel Resolve & Ping-Pong Dilation
    const std::array<VkDescriptorPoolSize, 5u> Sizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,           16u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,           24u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,            24u },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            24u },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4u },
    }};
    VkDescriptorPoolCreateInfo RegistrySpec {};
    RegistrySpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    RegistrySpec.maxSets       = 16u;
    RegistrySpec.poolSizeCount = static_cast<uint32_t>(Sizes.size());
    RegistrySpec.pPoolSizes    = Sizes.data();
    // Skip RTX pool entry cleanly if extension not loaded to stay strict on non-RT Vulkan 1.2 drivers
    RegistrySpec.poolSizeCount = DeviceOwner->QueryHardwareRayQuerySupported() ? 5u : 4u;
    if (vkCreateDescriptorPool(Device, &RegistrySpec, nullptr, &DescriptorRegistry) != VK_SUCCESS)
    {
        return false;
    }

    // 2. MRT RenderSequence for UV G-Buffer Rasterization (4 x RGBA32F)
    {
        std::array<VkAttachmentDescription, 4u> Attachments {};
        std::array<VkAttachmentReference, 4u>   ColorRefs {};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            Attachments[I].format         = VK_FORMAT_R32G32B32A32_SFLOAT;
            Attachments[I].samples        = VK_SAMPLE_COUNT_1_BIT;
            Attachments[I].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            Attachments[I].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            Attachments[I].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            Attachments[I].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            Attachments[I].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            Attachments[I].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ColorRefs[I] = { I, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        }
        VkSubpassDescription Sub {};
        Sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Sub.colorAttachmentCount = 4u;
        Sub.pColorAttachments    = ColorRefs.data();

        VkSubpassDependency Dep {};
        Dep.srcSubpass    = 0u;
        Dep.dstSubpass    = VK_SUBPASS_EXTERNAL;
        Dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dep.dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        Dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo RpSpec {};
        RpSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RpSpec.attachmentCount = 4u;
        RpSpec.pAttachments    = Attachments.data();
        RpSpec.subpassCount    = 1u;
        RpSpec.pSubpasses      = &Sub;
        RpSpec.dependencyCount = 1u;
        RpSpec.pDependencies   = &Dep;
        if (vkCreateRenderPass(Device, &RpSpec, nullptr, &UvGbufferRenderSequence) != VK_SUCCESS)
        {
            return false;
        }
    }

    // 3. UV Rasterization Graphics Program
    {
        UvRasterSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        });
        UvRasterProgramLayout = CreateProgramLayout(Device, UvRasterSetLayout);
        UvRasterDescriptorSet = AllocateSet(Device, DescriptorRegistry, UvRasterSetLayout);

        VkShaderModule VertUnit = DeviceOwner->LoadShaderBinary(ShaderDir / "BakeLowUvRasterization.vert.spv");
        VkShaderModule FragUnit = DeviceOwner->LoadShaderBinary(ShaderDir / "BakeLowUvRasterization.frag.spv");
        if (VertUnit == VK_NULL_HANDLE || FragUnit == VK_NULL_HANDLE)
        {
            if (VertUnit != VK_NULL_HANDLE) vkDestroyShaderModule(Device, VertUnit, nullptr);
            if (FragUnit != VK_NULL_HANDLE) vkDestroyShaderModule(Device, FragUnit, nullptr);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2u> Shaders {};
        Shaders[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        Shaders[0].module = VertUnit;
        Shaders[0].pName  = "VertexEntry";
        Shaders[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Shaders[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        Shaders[1].module = FragUnit;
        Shaders[1].pName  = "FragmentEntry";

        VkPipelineVertexInputStateCreateInfo VertIn {};
        VertIn.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo InputAsm {};
        InputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo ViewportCfg {};
        ViewportCfg.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewportCfg.viewportCount = 1u;
        ViewportCfg.scissorCount  = 1u;

        VkPipelineRasterizationStateCreateInfo RasterCfg {};
        RasterCfg.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        RasterCfg.polygonMode = VK_POLYGON_MODE_FILL;
        RasterCfg.cullMode    = VK_CULL_MODE_NONE; // Rasterize both front and mirrored UV islands
        RasterCfg.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        RasterCfg.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo MsaaCfg {};
        MsaaCfg.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        MsaaCfg.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        std::array<VkPipelineColorBlendAttachmentState, 4u> BlendAtts {};
        for (auto& A : BlendAtts)
        {
            A.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            A.blendEnable    = VK_FALSE;
        }
        VkPipelineColorBlendStateCreateInfo BlendCfg {};
        BlendCfg.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        BlendCfg.attachmentCount = 4u;
        BlendCfg.pAttachments    = BlendAtts.data();

        const std::array<VkDynamicState, 2u> DynList { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo DynCfg {};
        DynCfg.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynCfg.dynamicStateCount = 2u;
        DynCfg.pDynamicStates    = DynList.data();

        VkGraphicsPipelineCreateInfo GfxSpec {};
        GfxSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        GfxSpec.stageCount          = 2u;
        GfxSpec.pStages             = Shaders.data();
        GfxSpec.pVertexInputState   = &VertIn;
        GfxSpec.pInputAssemblyState = &InputAsm;
        GfxSpec.pViewportState      = &ViewportCfg;
        GfxSpec.pRasterizationState = &RasterCfg;
        GfxSpec.pMultisampleState   = &MsaaCfg;
        GfxSpec.pColorBlendState    = &BlendCfg;
        GfxSpec.pDynamicState       = &DynCfg;
        GfxSpec.layout              = UvRasterProgramLayout;
        GfxSpec.renderPass          = UvGbufferRenderSequence;

        const VkResult Res = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &GfxSpec, nullptr, &UvRasterProgram);
        vkDestroyShaderModule(Device, VertUnit, nullptr);
        vkDestroyShaderModule(Device, FragUnit, nullptr);
        if (Res != VK_SUCCESS)
        {
            return false;
        }
    }

    // 4. Surface Transfer Compute (GTX BVH4)
    {
        SurfaceTransferGtxSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        });
        SurfaceTransferGtxProgramLayout = CreateProgramLayout(Device, SurfaceTransferGtxSetLayout);
        SurfaceTransferGtxDescriptorSet = AllocateSet(Device, DescriptorRegistry, SurfaceTransferGtxSetLayout);
        SurfaceTransferGtxProgram       = CreateComputeProgram(
            Device,
            SurfaceTransferGtxProgramLayout,
            *DeviceOwner,
            ShaderDir / "BakeSurfaceTransferCompute_GTX.comp.spv");
        if (SurfaceTransferGtxProgram == VK_NULL_HANDLE)
        {
            return false;
        }
    }

    // 5. Channel Resolve Compute (all 17 targets from RGBA32F -> RGBA8 Array)
    {
        ChannelResolveSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 6u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 7u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        });
        ChannelResolveProgramLayout = CreateProgramLayout(Device, ChannelResolveSetLayout);
        ChannelResolveDescriptorSet = AllocateSet(Device, DescriptorRegistry, ChannelResolveSetLayout);
        ChannelResolveProgram       = CreateComputeProgram(
            Device,
            ChannelResolveProgramLayout,
            *DeviceOwner,
            ShaderDir / "BakeChannelResolveCompute.comp.spv");
        if (ChannelResolveProgram == VK_NULL_HANDLE)
        {
            return false;
        }
    }

    // 6. Ping-Pong Dilation Compute
    {
        DilationSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        });
        DilationProgramLayout = CreateProgramLayout(Device, DilationSetLayout);
        DilationSetAtoB       = AllocateSet(Device, DescriptorRegistry, DilationSetLayout);
        DilationSetBtoA       = AllocateSet(Device, DescriptorRegistry, DilationSetLayout);
        DilationProgram       = CreateComputeProgram(
            Device,
            DilationProgramLayout,
            *DeviceOwner,
            ShaderDir / "BakeDilationCompute.comp.spv");
        if (DilationProgram == VK_NULL_HANDLE)
        {
            return false;
        }
    }

    // 7. Optional Hardware RTX RayQuery Compute Variants (only built when device supports VK_KHR_ray_query)
    RtxProgramsLoaded = false;
    if (DeviceOwner->QueryHardwareRayQuerySupported() &&
        std::filesystem::exists(ShaderDir / "BakeSurfaceTransferCompute_RTX.comp.spv"))
    {
        SurfaceTransferRtxSetLayout = CreateSetLayout(Device, {
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,           1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,           1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,            1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        });
        SurfaceTransferRtxProgramLayout = CreateProgramLayout(Device, SurfaceTransferRtxSetLayout);
        SurfaceTransferRtxDescriptorSet = AllocateSet(Device, DescriptorRegistry, SurfaceTransferRtxSetLayout);
        SurfaceTransferRtxProgram       = CreateComputeProgram(
            Device,
            SurfaceTransferRtxProgramLayout,
            *DeviceOwner,
            ShaderDir / "BakeSurfaceTransferCompute_RTX.comp.spv");

        RtxProgramsLoaded = (SurfaceTransferRtxProgram != VK_NULL_HANDLE);
    }

    return true;
}

void GpuTextureBaker::DestroyShaderPrograms() noexcept
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

    DestroyProg(UvRasterProgram, UvRasterProgramLayout, UvRasterSetLayout);
    DestroyProg(SurfaceTransferGtxProgram, SurfaceTransferGtxProgramLayout, SurfaceTransferGtxSetLayout);
    DestroyProg(SurfaceTransferRtxProgram, SurfaceTransferRtxProgramLayout, SurfaceTransferRtxSetLayout);
    DestroyProg(ChannelResolveProgram, ChannelResolveProgramLayout, ChannelResolveSetLayout);
    DestroyProg(DilationProgram, DilationProgramLayout, DilationSetLayout);

    if (UvGbufferRenderSequence != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(Device, UvGbufferRenderSequence, nullptr);
        UvGbufferRenderSequence = VK_NULL_HANDLE;
    }
    if (DescriptorRegistry != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device, DescriptorRegistry, nullptr);
        DescriptorRegistry = VK_NULL_HANDLE;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SURFACE & VOLUME ALLOCATORS
//------------------------------------------------------------------------------------------------------------------------

bool GpuTextureBaker::AllocateArrayImage(uint32_t ResPx, uint32_t Layers, ArrayImageBundle& OutBundle) const
{
    ReleaseArrayImage(OutBundle);
    if (DeviceOwner == nullptr || ResPx == 0u || Layers == 0u)
    {
        return false;
    }
    VkDevice Device = DeviceOwner->AccessDevice();

    VkImageCreateInfo ImgSpec {};
    ImgSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImgSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImgSpec.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ImgSpec.extent        = { ResPx, ResPx, 1u };
    ImgSpec.mipLevels     = 1u;
    ImgSpec.arrayLayers   = Layers;
    ImgSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImgSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImgSpec.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImgSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImgSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(Device, &ImgSpec, nullptr, &OutBundle.ImageHandle) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements MemReq {};
    vkGetImageMemoryRequirements(Device, OutBundle.ImageHandle, &MemReq);

    VkPhysicalDeviceMemoryProperties MemProps {};
    vkGetPhysicalDeviceMemoryProperties(DeviceOwner->AccessPhysicalDevice(), &MemProps);
    uint32_t MemIdx = UINT32_MAX;
    for (uint32_t I = 0u; I < MemProps.memoryTypeCount; ++I)
    {
        if ((MemReq.memoryTypeBits & (1u << I)) != 0u &&
            (MemProps.memoryTypes[I].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u)
        {
            MemIdx = I;
            break;
        }
    }
    if (MemIdx == UINT32_MAX)
    {
        ReleaseArrayImage(OutBundle);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec {};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = MemReq.size;
    AllocSpec.memoryTypeIndex = MemIdx;
    if (vkAllocateMemory(Device, &AllocSpec, nullptr, &OutBundle.MemoryHandle) != VK_SUCCESS)
    {
        ReleaseArrayImage(OutBundle);
        return false;
    }
    vkBindImageMemory(Device, OutBundle.ImageHandle, OutBundle.MemoryHandle, 0ull);

    VkImageViewCreateInfo ViewSpec {};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutBundle.ImageHandle;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = Layers;
    if (vkCreateImageView(Device, &ViewSpec, nullptr, &OutBundle.ViewHandle) != VK_SUCCESS)
    {
        ReleaseArrayImage(OutBundle);
        return false;
    }

    OutBundle.WidthPx    = ResPx;
    OutBundle.HeightPx   = ResPx;
    OutBundle.LayerCount = Layers;
    return true;
}

void GpuTextureBaker::ReleaseArrayImage(ArrayImageBundle& Bundle) const noexcept
{
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return;
    }
    VkDevice Device = DeviceOwner->AccessDevice();
    if (Bundle.ViewHandle != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Device, Bundle.ViewHandle, nullptr);
        Bundle.ViewHandle = VK_NULL_HANDLE;
    }
    if (Bundle.ImageHandle != VK_NULL_HANDLE)
    {
        vkDestroyImage(Device, Bundle.ImageHandle, nullptr);
        Bundle.ImageHandle = VK_NULL_HANDLE;
    }
    if (Bundle.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(Device, Bundle.MemoryHandle, nullptr);
        Bundle.MemoryHandle = VK_NULL_HANDLE;
    }
    Bundle.WidthPx    = 0u;
    Bundle.HeightPx   = 0u;
    Bundle.LayerCount = 0u;
}

bool GpuTextureBaker::EnsureSurfaceResources(uint32_t ResolutionPx)
{
    if (AllocatedSurfaceResPx == ResolutionPx && UvGbufferTarget != VK_NULL_HANDLE)
    {
        return true;
    }
    ReleaseSurfaceResources();

    const VkImageUsageFlags FloatGbufUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags FloatCompUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    LowPosMaskImage      = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatGbufUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    LowNormPartImage     = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatGbufUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    LowTangIslandImage   = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatGbufUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    LowBitanWireImage    = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatGbufUsage, VK_IMAGE_ASPECT_COLOR_BIT);

    HighPosHeightImage   = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatCompUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    HighNormPartImage    = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatCompUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    OcclusionStatsImage  = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatCompUsage, VK_IMAGE_ASPECT_COLOR_BIT);
    BentNormalWorldImage = DeviceOwner->AllocateImage2D(ResolutionPx, ResolutionPx, VK_FORMAT_R32G32B32A32_SFLOAT, FloatCompUsage, VK_IMAGE_ASPECT_COLOR_BIT);

    if (!AllocateArrayImage(ResolutionPx, SurfaceTargetCount, ChannelArrayA) ||
        !AllocateArrayImage(ResolutionPx, SurfaceTargetCount, ChannelArrayB))
    {
        ReleaseSurfaceResources();
        return false;
    }
    ChannelArrayViewA = ChannelArrayA.ViewHandle;
    ChannelArrayViewB = ChannelArrayB.ViewHandle;

    ChannelReadbackBuffer = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(ResolutionPx) * ResolutionPx * 4ull,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    const std::array<VkImageView, 4u> MrtViews {
        LowPosMaskImage.ViewHandle,
        LowNormPartImage.ViewHandle,
        LowTangIslandImage.ViewHandle,
        LowBitanWireImage.ViewHandle,
    };
    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = UvGbufferRenderSequence;
    FbSpec.attachmentCount = 4u;
    FbSpec.pAttachments    = MrtViews.data();
    FbSpec.width           = ResolutionPx;
    FbSpec.height          = ResolutionPx;
    FbSpec.layers          = 1u;
    if (vkCreateFramebuffer(DeviceOwner->AccessDevice(), &FbSpec, nullptr, &UvGbufferTarget) != VK_SUCCESS)
    {
        ReleaseSurfaceResources();
        return false;
    }

    AllocatedSurfaceResPx = ResolutionPx;
    return true;
}

void GpuTextureBaker::ReleaseSurfaceResources() noexcept
{
    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        VkDevice Device = DeviceOwner->AccessDevice();
        vkDeviceWaitIdle(Device);
        if (UvGbufferTarget != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(Device, UvGbufferTarget, nullptr);
            UvGbufferTarget = VK_NULL_HANDLE;
        }
        DeviceOwner->ReleaseBuffer(ChannelReadbackBuffer);
        ReleaseArrayImage(ChannelArrayA);
        ReleaseArrayImage(ChannelArrayB);
        ChannelArrayViewA = VK_NULL_HANDLE;
        ChannelArrayViewB = VK_NULL_HANDLE;
        DeviceOwner->ReleaseImage2D(LowPosMaskImage);
        DeviceOwner->ReleaseImage2D(LowNormPartImage);
        DeviceOwner->ReleaseImage2D(LowTangIslandImage);
        DeviceOwner->ReleaseImage2D(LowBitanWireImage);
        DeviceOwner->ReleaseImage2D(HighPosHeightImage);
        DeviceOwner->ReleaseImage2D(HighNormPartImage);
        DeviceOwner->ReleaseImage2D(OcclusionStatsImage);
        DeviceOwner->ReleaseImage2D(BentNormalWorldImage);
    }
    AllocatedSurfaceResPx = 0u;
}

void GpuTextureBaker::TransitionImageLayout(
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
//                                              SURFACE BAKING DISPATCH
//------------------------------------------------------------------------------------------------------------------------

BakeCompletionSummary GpuTextureBaker::ExecuteSurfaceBake(const SurfaceBakeExecutionSpec& Spec)
{
    BakeCompletionSummary Summary {};
    if (!Initialized || !Accel.Ready())
    {
        Summary.DiagnosticText = "Baker or study acceleration structure uninitialized.";
        return Summary;
    }

    const uint32_t ResPx = std::clamp(Spec.ResolutionPx, 64u, 2048u);
    if (!EnsureSurfaceResources(ResPx))
    {
        Summary.DiagnosticText = "Failed allocating GPU surface bake targets.";
        return Summary;
    }

    const auto T0 = std::chrono::steady_clock::now();
    const BakeAccelerationEngine ActiveBackend = QueryActiveBackend(Spec.PreferComputeBvh4);
    Summary.ExecutedBackend = ActiveBackend;

    // 1. Update Surface Uniforms
    GpuSurfaceBakeConfig Cfg {};
    Cfg.ResolutionPx           = ResPx;
    Cfg.HighTriangleCount      = Accel.QueryHighTriangleCount();
    Cfg.WideEntryCount         = Accel.QueryWideEntryCount();
    Cfg.AoSampleCount          = std::clamp(Spec.AoSampleCount, 4u, 64u);
    Cfg.FrontCageMeters        = std::max(1.0e-4f, Spec.FrontCageMeters);
    Cfg.RearCageMeters         = std::max(1.0e-4f, Spec.RearCageMeters);
    Cfg.AoMaxRayDistanceMeters = std::max(0.02f, Spec.AoMaxRayDistanceMeters);
    Cfg.TangentSignY           = (Spec.TangentSignY < 0.0f) ? -1.0f : 1.0f;
    Cfg.BoundsMin[0]           = ActiveStudy.BoundsMin.X;
    Cfg.BoundsMin[1]           = ActiveStudy.BoundsMin.Y;
    Cfg.BoundsMin[2]           = ActiveStudy.BoundsMin.Z;
    Cfg.BoundsDiagMeters       = ActiveStudy.BoundsDiagMeters;
    Cfg.BoundsMax[0]           = ActiveStudy.BoundsMax.X;
    Cfg.BoundsMax[1]           = ActiveStudy.BoundsMax.Y;
    Cfg.BoundsMax[2]           = ActiveStudy.BoundsMax.Z;
    Cfg.BevelSigmaMeters       = std::max(1.0e-4f, ActiveStudy.BoundsDiagMeters * 0.012f);
    Cfg.ActiveTargetBitmask    = Spec.ActiveTargetBitmask;
    Cfg.DilationRadiusPx       = std::clamp(Spec.EdgePaddingPx, 0u, 32u);
    std::memcpy(SurfaceUniformBuffer.HostAddress, &Cfg, sizeof(Cfg));

    // 2. Refresh Descriptor Bindings
    VkDevice Device = DeviceOwner->AccessDevice();
    std::vector<VkWriteDescriptorSet>  Writes;
    std::deque<VkDescriptorBufferInfo> BufSlots;
    std::deque<VkDescriptorImageInfo>  ImgSlots;

    AttachBufferSlot(Writes, BufSlots, UvRasterDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
    AttachBufferSlot(Writes, BufSlots, UvRasterDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessLowVertexBuffer());

    const bool UseRtx = (ActiveBackend == BakeAccelerationEngine::RtxHardwareRayQuery);
    VkWriteDescriptorSetAccelerationStructureKHR AccelWrite {};
    VkAccelerationStructureKHR TlasHandle = Accel.AccessTopLevelAccel();
    if (UseRtx)
    {
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferRtxDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowPosMaskImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferRtxDescriptorSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessHighTriangleBuffer());
        AccelWrite.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        AccelWrite.accelerationStructureCount = 1u;
        AccelWrite.pAccelerationStructures    = &TlasHandle;
        VkWriteDescriptorSet AccW {};
        AccW.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        AccW.pNext           = &AccelWrite;
        AccW.dstSet          = SurfaceTransferRtxDescriptorSet;
        AccW.dstBinding      = 4u;
        AccW.descriptorCount = 1u;
        AccW.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        Writes.push_back(AccW);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HighPosHeightImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HighNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, OcclusionStatsImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferRtxDescriptorSet, 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BentNormalWorldImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
    }
    else
    {
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferGtxDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowPosMaskImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferGtxDescriptorSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessHighTriangleBuffer());
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferGtxDescriptorSet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessWideEntryBuffer());
        AttachBufferSlot(Writes, BufSlots, SurfaceTransferGtxDescriptorSet, 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Accel.AccessBvhPrimitiveBuffer());
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HighPosHeightImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 7u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HighNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, OcclusionStatsImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
        AttachImageSlot (Writes, ImgSlots, SurfaceTransferGtxDescriptorSet, 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, BentNormalWorldImage.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);
    }

    AttachBufferSlot(Writes, BufSlots, ChannelResolveDescriptorSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowPosMaskImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 3u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowTangIslandImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 4u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, LowBitanWireImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 5u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, HighPosHeightImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 6u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, HighNormPartImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 7u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, OcclusionStatsImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BentNormalWorldImage.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, ChannelResolveDescriptorSet, 9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ChannelArrayA.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);

    AttachBufferSlot(Writes, BufSlots, DilationSetAtoB, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
    AttachImageSlot (Writes, ImgSlots, DilationSetAtoB, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ChannelArrayA.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, DilationSetAtoB, 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ChannelArrayB.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);

    AttachBufferSlot(Writes, BufSlots, DilationSetBtoA, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SurfaceUniformBuffer.BufferHandle);
    AttachImageSlot (Writes, ImgSlots, DilationSetBtoA, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ChannelArrayB.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    AttachImageSlot (Writes, ImgSlots, DilationSetBtoA, 2u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ChannelArrayA.ViewHandle, VK_IMAGE_LAYOUT_GENERAL);

    vkUpdateDescriptorSets(Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    // 3. Record Unified Command Buffer (Stages 1..4)
    VkCommandBuffer Cmd = DeviceOwner->BeginSingleTimeCommands();

    // Stage 1: GPU UV G-buffer Rasterization
    std::array<VkClearValue, 4u> Clears {};
    Clears[0].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    Clears[1].color = {{ 0.0f, 0.0f, 1.0f, 0.0f }};
    Clears[2].color = {{ 1.0f, 0.0f, 0.0f, 0.0f }};
    Clears[3].color = {{ 0.0f, 1.0f, 0.0f, 0.0f }};

    VkRenderPassBeginInfo RpBegin {};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = UvGbufferRenderSequence;
    RpBegin.framebuffer       = UvGbufferTarget;
    RpBegin.renderArea.extent = { ResPx, ResPx };
    RpBegin.clearValueCount   = 4u;
    RpBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport VP { 0.0f, 0.0f, static_cast<float>(ResPx), static_cast<float>(ResPx), 0.0f, 1.0f };
    VkRect2D   Sc { { 0, 0 }, { ResPx, ResPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Sc);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UvRasterProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UvRasterProgramLayout, 0u, 1u, &UvRasterDescriptorSet, 0u, nullptr);
    vkCmdDraw(Cmd, Accel.QueryLowVertexCount(), 1u, 0u, 0u);
    vkCmdEndRenderPass(Cmd);

    // Prepare intermediate storage images for compute write
    TransitionImageLayout(Cmd, HighPosHeightImage.ImageHandle,   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);
    TransitionImageLayout(Cmd, HighNormPartImage.ImageHandle,    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);
    TransitionImageLayout(Cmd, OcclusionStatsImage.ImageHandle,  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);
    TransitionImageLayout(Cmd, BentNormalWorldImage.ImageHandle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1u);
    TransitionImageLayout(Cmd, ChannelArrayA.ImageHandle,        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, SurfaceTargetCount);
    TransitionImageLayout(Cmd, ChannelArrayB.ImageHandle,        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, SurfaceTargetCount);

    const uint32_t GroupsXY = (ResPx + 7u) / 8u;

    // Stage 2: Raycast Surface Transfer + AO / BentNormal / Thickness (GTX or RTX)
    if (UseRtx)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SurfaceTransferRtxProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SurfaceTransferRtxProgramLayout, 0u, 1u, &SurfaceTransferRtxDescriptorSet, 0u, nullptr);
    }
    else
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SurfaceTransferGtxProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, SurfaceTransferGtxProgramLayout, 0u, 1u, &SurfaceTransferGtxDescriptorSet, 0u, nullptr);
    }
    vkCmdDispatch(Cmd, GroupsXY, GroupsXY, 1u);

    // Transition High G-buffers -> SHADER_READ_ONLY_OPTIMAL for ChannelResolve
    TransitionImageLayout(Cmd, HighPosHeightImage.ImageHandle,   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u);
    TransitionImageLayout(Cmd, HighNormPartImage.ImageHandle,    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u);
    TransitionImageLayout(Cmd, OcclusionStatsImage.ImageHandle,  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u);
    TransitionImageLayout(Cmd, BentNormalWorldImage.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u);

    // Stage 3: Unquantized Metric Curvature + Bevel + All 17 Channels Resolve
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ChannelResolveProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ChannelResolveProgramLayout, 0u, 1u, &ChannelResolveDescriptorSet, 0u, nullptr);
    vkCmdDispatch(Cmd, GroupsXY, GroupsXY, 1u);

    // Stage 4: Ping-Pong UV Gutter Dilation
    uint32_t CurIndex = 0u;
    const uint32_t DilateRounds = std::clamp(Spec.EdgePaddingPx, 0u, 24u);
    for (uint32_t R = 0u; R < DilateRounds; ++R)
    {
        ArrayImageBundle& SrcArr = (CurIndex == 0u) ? ChannelArrayA : ChannelArrayB;
        ArrayImageBundle& DstArr = (CurIndex == 0u) ? ChannelArrayB : ChannelArrayA;
        VkDescriptorSet   Set    = (CurIndex == 0u) ? DilationSetAtoB : DilationSetBtoA;

        TransitionImageLayout(Cmd, SrcArr.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, SurfaceTargetCount);
        TransitionImageLayout(Cmd, DstArr.ImageHandle, (R == 0u) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, SurfaceTargetCount);

        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DilationProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DilationProgramLayout, 0u, 1u, &Set, 0u, nullptr);
        vkCmdDispatch(Cmd, GroupsXY, GroupsXY, SurfaceTargetCount);

        CurIndex = 1u - CurIndex;
    }

    ActiveChannelArrayIndex = CurIndex;
    ArrayImageBundle& FinalArr = (ActiveChannelArrayIndex == 0u) ? ChannelArrayA : ChannelArrayB;
    TransitionImageLayout(Cmd, FinalArr.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SurfaceTargetCount);

    const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(Cmd);
    if (!DeviceOwner->AwaitTimelineTick(Tick, 60000000000ull))
    {
        vkQueueWaitIdle(DeviceOwner->AccessQueue());
        DeviceOwner->FreeCommandBuffer(Cmd);
        Summary.DiagnosticText = "GPU surface bake timed out.";
        return Summary;
    }
    DeviceOwner->FreeCommandBuffer(Cmd);

    // 4. Readback all 17 channel layers cleanly for interactive inspection & instant PNG export
    const uint64_t LayerBytes = static_cast<uint64_t>(ResPx) * ResPx * 4ull;
    for (uint32_t Layer = 0u; Layer < SurfaceTargetCount; ++Layer)
    {
        VkCommandBuffer CopyCmd = DeviceOwner->BeginSingleTimeCommands();
        VkBufferImageCopy CopyRegion {};
        CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        CopyRegion.imageSubresource.baseArrayLayer = Layer;
        CopyRegion.imageSubresource.layerCount     = 1u;
        CopyRegion.imageExtent                     = { ResPx, ResPx, 1u };
        vkCmdCopyImageToBuffer(
            CopyCmd,
            FinalArr.ImageHandle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ChannelReadbackBuffer.BufferHandle,
            1u,
            &CopyRegion);
        const uint64_t CopyTick = DeviceOwner->DispatchCommandBufferAsync(CopyCmd);
        (void)DeviceOwner->AwaitTimelineTick(CopyTick);
        DeviceOwner->FreeCommandBuffer(CopyCmd);

        CachedSurfaceChannelsRgba8[Layer].resize(static_cast<std::size_t>(LayerBytes));
        if (ChannelReadbackBuffer.HostAddress != nullptr)
        {
            std::memcpy(CachedSurfaceChannelsRgba8[Layer].data(), ChannelReadbackBuffer.HostAddress, static_cast<std::size_t>(LayerBytes));
        }
    }

    // Leave active array in SHADER_READ_ONLY_OPTIMAL so 3D PBR Preview shader samples it cleanly
    VkCommandBuffer LayoutCmd = DeviceOwner->BeginSingleTimeCommands();
    TransitionImageLayout(LayoutCmd, FinalArr.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, SurfaceTargetCount);
    const uint64_t LayoutTick = DeviceOwner->DispatchCommandBufferAsync(LayoutCmd);
    (void)DeviceOwner->AwaitTimelineTick(LayoutTick);
    DeviceOwner->FreeCommandBuffer(LayoutCmd);

    const auto T1 = std::chrono::steady_clock::now();
    SurfaceTexturesReady     = true;
    Summary.SurfaceSucceeded = true;
    Summary.SurfaceElapsedMs = std::chrono::duration<double, std::milli>(T1 - T0).count();
    Summary.DiagnosticText   = (ActiveBackend == BakeAccelerationEngine::RtxHardwareRayQuery)
        ? "RTX RayQuery Surface Bake Complete"
        : "GTX BVH4 Compute Surface Bake Complete";
    return Summary;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL EXPORT
//------------------------------------------------------------------------------------------------------------------------

bool GpuTextureBaker::ExportSurfaceChannelPng(
    uint32_t                     ChannelSlot,
    const std::filesystem::path& OutputPngPath) const
{
    if (ChannelSlot >= SurfaceTargetCount || AllocatedSurfaceResPx == 0u)
    {
        return false;
    }
    const auto& Rgba = CachedSurfaceChannelsRgba8[ChannelSlot];
    const std::size_t Expected = static_cast<std::size_t>(AllocatedSurfaceResPx) * AllocatedSurfaceResPx * 4u;
    if (Rgba.size() < Expected)
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
               static_cast<int>(AllocatedSurfaceResPx),
               static_cast<int>(AllocatedSurfaceResPx),
               4,
               Rgba.data(),
               static_cast<int>(AllocatedSurfaceResPx * 4u)) != 0;
}

} // namespace Frontier::GeometricRaster::Baking
