/*============================================================================================================================================
                                                          TEXTUREBAKERRASTERIZER.CPP
============================================================================================================================================*/
// 🧩 Implementation of interactive Vulkan 3D studio PBR + live baked texture + High-Poly wire + Cage overlay rasterizer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/TextureBakerRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

namespace Frontier::GeometricRaster::Baking
{

namespace
{

constexpr uint64_t UniformSlotStrideBytes = 256ull;
constexpr uint32_t UniformSlotCount       = 3u; // 0 = Solid, 1 = HighWire, 2 = Cage

[[nodiscard]] std::filesystem::path ResolveBakingPreviewShaderDir(const std::filesystem::path& ExplicitDir)
{
    if (!ExplicitDir.empty() && std::filesystem::exists(ExplicitDir / "BakeViewportPreview.vert.spv"))
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
        if (std::filesystem::exists(Candidate / "BakeViewportPreview.vert.spv"))
        {
            return Candidate;
        }
    }
    return ExplicitDir;
}

} // namespace

TextureBakerRasterizer::~TextureBakerRasterizer()
{
    TeardownRasterizer();
}

void TextureBakerRasterizer::AlignCameraToStudy(const PreparedStudyPair& Study, BakeOrbitCamera& OutCamera) noexcept
{
    OutCamera.TargetPivot = {
        0.5f * (Study.BoundsMin.X + Study.BoundsMax.X),
        0.5f * (Study.BoundsMin.Y + Study.BoundsMax.Y),
        0.5f * (Study.BoundsMin.Z + Study.BoundsMax.Z),
    };
    OutCamera.YawRadians     = 0.72f;
    OutCamera.PitchRadians   = 0.36f;
    OutCamera.DistanceMeters = std::max(1.20f, Study.BoundsDiagMeters * 1.18f);
    OutCamera.VerticalFovDeg = 42.0f;
}

bool TextureBakerRasterizer::InitializeRasterizer(
    VulkanInterchange*           BoundDevice,
    GpuTextureBaker*             BoundBaker,
    const std::filesystem::path& ShaderDirectory)
{
    TeardownRasterizer();
    DeviceOwner = BoundDevice;
    BakerRef    = BoundBaker;
    if (DeviceOwner == nullptr || !DeviceOwner->Ready() || BakerRef == nullptr || !BakerRef->Ready())
    {
        return false;
    }

    UniformRingBuffer = DeviceOwner->AllocateBuffer(
        UniformSlotStrideBytes * UniformSlotCount,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (UniformRingBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownRasterizer();
        return false;
    }

    const std::filesystem::path ShaderDir = ResolveBakingPreviewShaderDir(ShaderDirectory);
    if (!CreatePrograms(ShaderDir))
    {
        TeardownRasterizer();
        return false;
    }

    Initialized = true;
    return true;
}

void TextureBakerRasterizer::TeardownRasterizer() noexcept
{
    ReleaseRenderTargets();
    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        vkDeviceWaitIdle(DeviceOwner->AccessDevice());
        DeviceOwner->ReleaseBuffer(UniformRingBuffer);
        DestroyPrograms();
    }
    Initialized = false;
    DeviceOwner = nullptr;
    BakerRef    = nullptr;
}

bool TextureBakerRasterizer::CreatePrograms(const std::filesystem::path& ShaderDir)
{
    VkDevice Device = DeviceOwner->AccessDevice();

    const std::array<VkDescriptorPoolSize, 4u> PoolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         4u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          4u },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                4u },
    }};
    VkDescriptorPoolCreateInfo PoolSpec {};
    PoolSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolSpec.maxSets       = 4u;
    PoolSpec.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    PoolSpec.pPoolSizes    = PoolSizes.data();
    if (vkCreateDescriptorPool(Device, &PoolSpec, nullptr, &DescriptorRegistry) != VK_SUCCESS)
    {
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 4u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_SAMPLER,                1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};
    VkDescriptorSetLayoutCreateInfo SetSpec {};
    SetSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetSpec.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetSpec.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(Device, &SetSpec, nullptr, &PreviewSetLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo LayoutSpec {};
    LayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutSpec.setLayoutCount = 1u;
    LayoutSpec.pSetLayouts    = &PreviewSetLayout;
    if (vkCreatePipelineLayout(Device, &LayoutSpec, nullptr, &PreviewProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    const std::array<VkDescriptorSetLayout, 3u> Layouts { PreviewSetLayout, PreviewSetLayout, PreviewSetLayout };
    std::array<VkDescriptorSet, 3u>             Sets {};
    VkDescriptorSetAllocateInfo AllocSpec {};
    AllocSpec.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocSpec.descriptorPool     = DescriptorRegistry;
    AllocSpec.descriptorSetCount = 3u;
    AllocSpec.pSetLayouts        = Layouts.data();
    if (vkAllocateDescriptorSets(Device, &AllocSpec, Sets.data()) != VK_SUCCESS)
    {
        return false;
    }
    SolidDescriptorSet    = Sets[0];
    HighWireDescriptorSet = Sets[1];
    CageDescriptorSet     = Sets[2];

    // RenderSequence (Color RGBA8 + Depth D32F)
    std::array<VkAttachmentDescription, 2u> Atts {};
    Atts[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Atts[0].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    Atts[1].format         = VK_FORMAT_D32_SFLOAT;
    Atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Atts[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ColorRef { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference DepthRef { 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Sub {};
    Sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Sub.colorAttachmentCount    = 1u;
    Sub.pColorAttachments       = &ColorRef;
    Sub.pDepthStencilAttachment = &DepthRef;

    VkRenderPassCreateInfo RpSpec {};
    RpSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RpSpec.attachmentCount = 2u;
    RpSpec.pAttachments    = Atts.data();
    RpSpec.subpassCount    = 1u;
    RpSpec.pSubpasses      = &Sub;
    if (vkCreateRenderPass(Device, &RpSpec, nullptr, &PreviewRenderSequence) != VK_SUCCESS)
    {
        return false;
    }

    VkShaderModule VertUnit = DeviceOwner->LoadShaderBinary(ShaderDir / "BakeViewportPreview.vert.spv");
    VkShaderModule FragUnit = DeviceOwner->LoadShaderBinary(ShaderDir / "BakeViewportPreview.frag.spv");
    if (VertUnit == VK_NULL_HANDLE || FragUnit == VK_NULL_HANDLE)
    {
        if (VertUnit != VK_NULL_HANDLE) vkDestroyShaderModule(Device, VertUnit, nullptr);
        if (FragUnit != VK_NULL_HANDLE) vkDestroyShaderModule(Device, FragUnit, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> Stages {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertUnit;
    Stages[0].pName  = "VertexEntry";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragUnit;
    Stages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo VertIn {};
    VertIn.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAsm {};
    InputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo VpCfg {};
    VpCfg.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    VpCfg.viewportCount = 1u;
    VpCfg.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RasterCfg {};
    RasterCfg.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterCfg.polygonMode = VK_POLYGON_MODE_FILL;
    RasterCfg.cullMode    = VK_CULL_MODE_NONE; // Supports cutaway shells & double-sided visualization
    RasterCfg.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RasterCfg.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo MsaaCfg {};
    MsaaCfg.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MsaaCfg.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthCfg {};
    DepthCfg.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthCfg.depthTestEnable  = VK_TRUE;
    DepthCfg.depthWriteEnable = VK_TRUE;
    DepthCfg.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState BlendAtt {};
    BlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtt.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo BlendCfg {};
    BlendCfg.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendCfg.attachmentCount = 1u;
    BlendCfg.pAttachments    = &BlendAtt;

    const std::array<VkDynamicState, 2u> DynList { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynCfg {};
    DynCfg.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynCfg.dynamicStateCount = 2u;
    DynCfg.pDynamicStates    = DynList.data();

    VkGraphicsPipelineCreateInfo GfxSpec {};
    GfxSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    GfxSpec.stageCount          = 2u;
    GfxSpec.pStages             = Stages.data();
    GfxSpec.pVertexInputState   = &VertIn;
    GfxSpec.pInputAssemblyState = &InputAsm;
    GfxSpec.pViewportState      = &VpCfg;
    GfxSpec.pRasterizationState = &RasterCfg;
    GfxSpec.pMultisampleState   = &MsaaCfg;
    GfxSpec.pDepthStencilState  = &DepthCfg;
    GfxSpec.pColorBlendState    = &BlendCfg;
    GfxSpec.pDynamicState       = &DynCfg;
    GfxSpec.layout              = PreviewProgramLayout;
    GfxSpec.renderPass          = PreviewRenderSequence;

    const VkResult SolidRes = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &GfxSpec, nullptr, &SolidSurfaceProgram);

    // Translucent alpha-blended Wireframe / Cage overlay variant (depth write off, subtle bias)
    DepthCfg.depthWriteEnable         = VK_FALSE;
    RasterCfg.depthBiasEnable         = VK_TRUE;
    RasterCfg.depthBiasConstantFactor = -1.5f;
    RasterCfg.depthBiasSlopeFactor    = -1.5f;
    BlendAtt.blendEnable              = VK_TRUE;
    BlendAtt.srcColorBlendFactor      = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAtt.dstColorBlendFactor      = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtt.colorBlendOp             = VK_BLEND_OP_ADD;
    BlendAtt.srcAlphaBlendFactor      = VK_BLEND_FACTOR_ONE;
    BlendAtt.dstAlphaBlendFactor      = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtt.alphaBlendOp             = VK_BLEND_OP_ADD;

    const VkResult WireRes = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &GfxSpec, nullptr, &WireOverlayProgram);

    vkDestroyShaderModule(Device, VertUnit, nullptr);
    vkDestroyShaderModule(Device, FragUnit, nullptr);
    return (SolidRes == VK_SUCCESS && WireRes == VK_SUCCESS);
}

void TextureBakerRasterizer::DestroyPrograms() noexcept
{
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return;
    }
    VkDevice Device = DeviceOwner->AccessDevice();
    if (SolidSurfaceProgram != VK_NULL_HANDLE)  { vkDestroyPipeline(Device, SolidSurfaceProgram, nullptr); SolidSurfaceProgram = VK_NULL_HANDLE; }
    if (WireOverlayProgram != VK_NULL_HANDLE)   { vkDestroyPipeline(Device, WireOverlayProgram, nullptr); WireOverlayProgram = VK_NULL_HANDLE; }
    if (PreviewRenderSequence != VK_NULL_HANDLE) { vkDestroyRenderPass(Device, PreviewRenderSequence, nullptr); PreviewRenderSequence = VK_NULL_HANDLE; }
    if (PreviewProgramLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Device, PreviewProgramLayout, nullptr); PreviewProgramLayout = VK_NULL_HANDLE; }
    if (PreviewSetLayout != VK_NULL_HANDLE)     { vkDestroyDescriptorSetLayout(Device, PreviewSetLayout, nullptr); PreviewSetLayout = VK_NULL_HANDLE; }
    if (DescriptorRegistry != VK_NULL_HANDLE)   { vkDestroyDescriptorPool(Device, DescriptorRegistry, nullptr); DescriptorRegistry = VK_NULL_HANDLE; }
}

bool TextureBakerRasterizer::EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx)
{
    if (AllocatedWidthPx == WidthPx && AllocatedHeightPx == HeightPx && PreviewFramebuffer != VK_NULL_HANDLE)
    {
        return true;
    }
    ReleaseRenderTargets();

    ColorTargetImage = DeviceOwner->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    DepthTargetImage = DeviceOwner->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    ReadbackBuffer = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(WidthPx) * HeightPx * 4ull,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (ColorTargetImage.ViewHandle == VK_NULL_HANDLE ||
        DepthTargetImage.ViewHandle == VK_NULL_HANDLE ||
        ReadbackBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        ReleaseRenderTargets();
        return false;
    }

    const std::array<VkImageView, 2u> Views { ColorTargetImage.ViewHandle, DepthTargetImage.ViewHandle };
    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = PreviewRenderSequence;
    FbSpec.attachmentCount = 2u;
    FbSpec.pAttachments    = Views.data();
    FbSpec.width           = WidthPx;
    FbSpec.height          = HeightPx;
    FbSpec.layers          = 1u;
    if (vkCreateFramebuffer(DeviceOwner->AccessDevice(), &FbSpec, nullptr, &PreviewFramebuffer) != VK_SUCCESS)
    {
        ReleaseRenderTargets();
        return false;
    }

    AllocatedWidthPx  = WidthPx;
    AllocatedHeightPx = HeightPx;
    return true;
}

void TextureBakerRasterizer::ReleaseRenderTargets() noexcept
{
    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        vkDeviceWaitIdle(DeviceOwner->AccessDevice());
        if (PreviewFramebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(DeviceOwner->AccessDevice(), PreviewFramebuffer, nullptr);
            PreviewFramebuffer = VK_NULL_HANDLE;
        }
        DeviceOwner->ReleaseBuffer(ReadbackBuffer);
        DeviceOwner->ReleaseImage2D(ColorTargetImage);
        DeviceOwner->ReleaseImage2D(DepthTargetImage);
    }
    AllocatedWidthPx  = 0u;
    AllocatedHeightPx = 0u;
}

bool TextureBakerRasterizer::RenderSpatialViewport(const BakeViewportRenderSpec& Spec)
{
    if (!Initialized || BakerRef == nullptr || !BakerRef->QueryStudyUploaded())
    {
        return false;
    }

    const uint32_t W = std::clamp(Spec.WidthPx, 64u, 2048u);
    const uint32_t H = std::clamp(Spec.HeightPx, 64u, 2048u);
    if (!EnsureRenderTargets(W, H))
    {
        return false;
    }

    const BakingAccelerationStructure& Accel = BakerRef->InspectAccel();

    // Update Descriptor Sets pointing to live GPU channel array + Low / High vertex buffers
    std::vector<VkWriteDescriptorSet>   Writes;
    std::deque<VkDescriptorBufferInfo>  BInfos;
    std::deque<VkDescriptorImageInfo>   IInfos;

    const auto BindSet = [&](VkDescriptorSet DstSet, VkBuffer VertexBuf)
    {
        VkDescriptorBufferInfo Uni {};
        Uni.buffer = UniformRingBuffer.BufferHandle;
        Uni.offset = 0ull;
        Uni.range  = sizeof(GpuPreviewConfig);
        BInfos.push_back(Uni);
        VkWriteDescriptorSet W0 {};
        W0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        W0.dstSet = DstSet; W0.dstBinding = 0u; W0.descriptorCount = 1u;
        W0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        W0.pBufferInfo = &BInfos.back();
        Writes.push_back(W0);

        VkDescriptorBufferInfo Vb {};
        Vb.buffer = VertexBuf;
        Vb.offset = 0ull;
        Vb.range  = VK_WHOLE_SIZE;
        BInfos.push_back(Vb);
        VkWriteDescriptorSet W1 {};
        W1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        W1.dstSet = DstSet; W1.dstBinding = 1u; W1.descriptorCount = 1u;
        W1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        W1.pBufferInfo = &BInfos.back();
        Writes.push_back(W1);

        VkDescriptorImageInfo Img {};
        Img.imageView   = BakerRef->AccessBakedChannelArrayView();
        Img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        IInfos.push_back(Img);
        VkWriteDescriptorSet W2 {};
        W2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        W2.dstSet = DstSet; W2.dstBinding = 2u; W2.descriptorCount = 1u;
        W2.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        W2.pImageInfo = &IInfos.back();
        Writes.push_back(W2);

        VkDescriptorImageInfo Samp {};
        Samp.sampler = BakerRef->AccessLinearSampler();
        IInfos.push_back(Samp);
        VkWriteDescriptorSet W3 {};
        W3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        W3.dstSet = DstSet; W3.dstBinding = 3u; W3.descriptorCount = 1u;
        W3.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        W3.pImageInfo = &IInfos.back();
        Writes.push_back(W3);
    };

    BindSet(SolidDescriptorSet,    Accel.AccessLowVertexBuffer());
    BindSet(HighWireDescriptorSet, Accel.AccessHighWireVertexBuffer());
    BindSet(CageDescriptorSet,     Accel.AccessLowVertexBuffer());
    vkUpdateDescriptorSets(DeviceOwner->AccessDevice(), static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    // Camera clip transform (expand vertical FOV in narrow split panes so horizontal study framing stays complete)
    const float Aspect = static_cast<float>(W) / static_cast<float>(H);
    const float SafeAspect = std::max(0.20f, Aspect);
    float EffectiveFovDeg = Spec.Camera.VerticalFovDeg;
    if (SafeAspect < 1.0f)
    {
        const float HalfRad = (Spec.Camera.VerticalFovDeg * 0.5f) * 3.14159265f / 180.0f;
        EffectiveFovDeg = std::clamp(
            2.0f * std::atan(std::tan(HalfRad) / SafeAspect) * (180.0f / 3.14159265f),
            Spec.Camera.VerticalFovDeg,
            108.0f);
    }
    const Mathematics::Axis3D Eye = Spec.Camera.TargetPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
        Spec.Camera.DistanceMeters,
        Spec.Camera.YawRadians,
        Spec.Camera.PitchRadians,
        Spec.Camera.Basis);
    Mathematics::Axis3D Right {}, Up {}, Fwd {};
    Mathematics::EvaluateOrthonormalCameraBasis(Eye, Spec.Camera.TargetPivot, Spec.Camera.Basis, Right, Up, Fwd);
    const float FarZ = std::max(50.0f, Spec.Camera.DistanceMeters * 8.0f);
    const Mathematics::ClipTransform4x4 Clip = Mathematics::ConstructVulkanPerspectiveViewClip(
        Eye, Right, Up, Fwd, EffectiveFovDeg, SafeAspect, 0.04f, FarZ);

    GpuPreviewConfig SolidCfg {};
    for (int R = 0; R < 4; ++R)
    {
        for (int C = 0; C < 4; ++C)
        {
            SolidCfg.ViewProjTransform[R * 4 + C] = Clip.Rows[R][C];
        }
    }
    SolidCfg.CameraEyeMode[0] = Eye.X;
    SolidCfg.CameraEyeMode[1] = Eye.Y;
    SolidCfg.CameraEyeMode[2] = Eye.Z;
    SolidCfg.CameraEyeMode[3] = static_cast<float>(Spec.InspectedTargetId);
    SolidCfg.OverlayFlagsPacked[0]  = Spec.SurfaceBaked ? 1.0f : 0.0f;
    SolidCfg.OverlayFlagsPacked[2]  = Spec.FrontCageMeters;
    SolidCfg.ViewportSizeTime[0] = static_cast<float>(W);
    SolidCfg.ViewportSizeTime[1] = static_cast<float>(H);

    // Slot 0: Lit Solid Low-Poly surface
    SolidCfg.ViewportSizeTime[3] = 0.0f;
    std::memcpy(static_cast<uint8_t*>(UniformRingBuffer.HostAddress) + 0ull, &SolidCfg, sizeof(SolidCfg));

    // Slot 1: High-Poly wireframe overlay
    GpuPreviewConfig HighWireCfg = SolidCfg;
    HighWireCfg.ViewportSizeTime[3] = 1.0f;
    HighWireCfg.OverlayColor[0] = 0.937f;
    HighWireCfg.OverlayColor[1] = 0.957f;
    HighWireCfg.OverlayColor[2] = 0.812f;
    HighWireCfg.OverlayColor[3] = 0.55f;
    std::memcpy(static_cast<uint8_t*>(UniformRingBuffer.HostAddress) + UniformSlotStrideBytes, &HighWireCfg, sizeof(HighWireCfg));

    // Slot 2: Translucent expanded Front Cage shell wireframe
    GpuPreviewConfig CageCfg = SolidCfg;
    CageCfg.ViewportSizeTime[3] = 2.0f;
    CageCfg.OverlayColor[0] = 0.25f;
    CageCfg.OverlayColor[1] = 0.72f;
    CageCfg.OverlayColor[2] = 0.96f;
    CageCfg.OverlayColor[3] = 0.42f;
    std::memcpy(static_cast<uint8_t*>(UniformRingBuffer.HostAddress) + UniformSlotStrideBytes * 2ull, &CageCfg, sizeof(CageCfg));

    VkCommandBuffer Cmd = DeviceOwner->BeginSingleTimeCommands();

    std::array<VkClearValue, 2u> Clears {};
    Clears[0].color        = {{ 0.078f, 0.084f, 0.102f, 1.0f }};
    Clears[1].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo RpBegin {};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = PreviewRenderSequence;
    RpBegin.framebuffer       = PreviewFramebuffer;
    RpBegin.renderArea.extent = { W, H };
    RpBegin.clearValueCount   = 2u;
    RpBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport VP { 0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H), 0.0f, 1.0f };
    VkRect2D   Sc { { 0, 0 }, { W, H } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Sc);

    // 1. Solid Lit PBR study
    const uint32_t Slot0Offset = 0u;
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SolidSurfaceProgram);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PreviewProgramLayout, 0u, 1u, &SolidDescriptorSet, 1u, &Slot0Offset);
    vkCmdDraw(Cmd, Accel.QueryLowVertexCount(), 1u, 0u, 0u);

    // 2. Optional High-Poly silhouette wireframe overlay
    if (Spec.ShowHighWireOverlay && Accel.QueryHighWireVertexCount() > 0u)
    {
        const uint32_t Slot1Offset = static_cast<uint32_t>(UniformSlotStrideBytes);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, WireOverlayProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PreviewProgramLayout, 0u, 1u, &HighWireDescriptorSet, 1u, &Slot1Offset);
        vkCmdDraw(Cmd, Accel.QueryHighWireVertexCount(), 1u, 0u, 0u);
    }

    // 3. Optional Translucent expanded Front Cage shell overlay
    if (Spec.ShowCageOverlay)
    {
        const uint32_t Slot2Offset = static_cast<uint32_t>(UniformSlotStrideBytes * 2ull);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, WireOverlayProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PreviewProgramLayout, 0u, 1u, &CageDescriptorSet, 1u, &Slot2Offset);
        vkCmdDraw(Cmd, Accel.QueryLowVertexCount(), 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Cmd);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { W, H, 1u };
    vkCmdCopyImageToBuffer(Cmd, ColorTargetImage.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer.BufferHandle, 1u, &CopyRegion);

    const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(Cmd);
    const bool Ok       = DeviceOwner->AwaitTimelineTick(Tick);
    DeviceOwner->FreeCommandBuffer(Cmd);
    if (!Ok)
    {
        return false;
    }

    const std::size_t ByteLen = static_cast<std::size_t>(W) * H * 4u;
    SpatialRgbaPixels.resize(ByteLen);
    if (ReadbackBuffer.HostAddress != nullptr)
    {
        std::memcpy(SpatialRgbaPixels.data(), ReadbackBuffer.HostAddress, ByteLen);
    }
    return true;
}

} // namespace Frontier::GeometricRaster::Baking
