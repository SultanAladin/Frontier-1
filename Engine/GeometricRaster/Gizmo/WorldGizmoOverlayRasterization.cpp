/*============================================================================================================================================
                                                      WORLDGIZMOOVERLAYRASTERIZATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of WorldGizmoOverlayRasterization executing GizmoRasterization.slang on Vulkan 1.2/1.3 over TieredWorldRenderSequence

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Gizmo/WorldGizmoOverlayRasterization.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

struct alignas(16) GPUWorldGizmoUniformBlock
{
    float    ViewProjection[4][4] {};
    uint32_t HoverSpacePacked[4]     {};
    float    HeadlightDirection[4]{};
};

void ComputeCameraBasis(
    const PerspectiveCameraBasis& Cam,
    Mathematics::Axis3D&          OutEye,
    Mathematics::Axis3D&          OutRight,
    Mathematics::Axis3D&          OutUp,
    Mathematics::Axis3D&          OutForward) noexcept
{
    OutEye     = Cam.CameraOrigin;
    OutForward = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    OutRight   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutForward, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    OutUp      = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutRight, OutForward), { 0.0f, 0.0f, 1.0f });
}

void DestroyProgramUnit(VkDevice DeviceHandle, auto& Unit) noexcept
{
    if (DeviceHandle == VK_NULL_HANDLE)
    {
        return;
    }
    if (Unit.ProgramHandle != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(DeviceHandle, Unit.ProgramHandle, nullptr);
        Unit.ProgramHandle = VK_NULL_HANDLE;
    }
    if (Unit.ProgramLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(DeviceHandle, Unit.ProgramLayout, nullptr);
        Unit.ProgramLayout = VK_NULL_HANDLE;
    }
    if (Unit.SlotLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(DeviceHandle, Unit.SlotLayout, nullptr);
        Unit.SlotLayout = VK_NULL_HANDLE;
    }
    if (Unit.VertexSpirvBinary != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(DeviceHandle, Unit.VertexSpirvBinary, nullptr);
        Unit.VertexSpirvBinary = VK_NULL_HANDLE;
    }
    if (Unit.FragmentSpirvBinary != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(DeviceHandle, Unit.FragmentSpirvBinary, nullptr);
        Unit.FragmentSpirvBinary = VK_NULL_HANDLE;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

WorldGizmoOverlayRasterization::~WorldGizmoOverlayRasterization()
{
    TeardownOverlay();
}

bool WorldGizmoOverlayRasterization::InitializeOverlay(
    VulkanInterchange*           BoundDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownOverlay();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    DeviceInterchange = BoundDevice;
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    // Attachment 0: RGBA8 world color (LOAD existing TieredWorldRenderSequence pixels, WRITE composited pixels)
    // Attachment 1: R32G32B32A32_UINT pick target (CLEAR to 0, WRITE GizmoHandleOrdinal in .w)
    // Attachment 2: D32_SFLOAT gizmo depth (CLEAR to 1.0)
    std::array<VkAttachmentDescription, 3u> Attachments {};
    Attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_R32G32B32A32_UINT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    Attachments[2].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const std::array<VkAttachmentReference, 2u> ColorRefs {{
        { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    }};
    VkAttachmentReference DepthRef { 2u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Subpass {};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = static_cast<uint32_t>(ColorRefs.size());
    Subpass.pColorAttachments       = ColorRefs.data();
    Subpass.pDepthStencilAttachment = &DepthRef;

    VkRenderPassCreateInfo RenderTargetSpec {};
    RenderTargetSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RenderTargetSpec.attachmentCount = static_cast<uint32_t>(Attachments.size());
    RenderTargetSpec.pAttachments    = Attachments.data();
    RenderTargetSpec.subpassCount    = 1u;
    RenderTargetSpec.pSubpasses      = &Subpass;

    if (vkCreateRenderPass(DeviceHandle, &RenderTargetSpec, nullptr, &OverlayRenderTargetFormat) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    // GPU-resident composite render pass: blit writes every Color/Pick pixel (DONT_CARE load), outputs SHADER_READ_ONLY_OPTIMAL for ImGui
    std::array<VkAttachmentDescription, 3u> GpuAtts = Attachments;
    GpuAtts[0].loadOp        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    GpuAtts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    GpuAtts[0].finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    GpuAtts[1].loadOp        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    GpuAtts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    GpuAtts[1].finalLayout   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    std::array<VkSubpassDependency, 2u> GpuDeps {};
    GpuDeps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    GpuDeps[0].dstSubpass      = 0u;
    GpuDeps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    GpuDeps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    GpuDeps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    GpuDeps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    GpuDeps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    GpuDeps[1].srcSubpass      = 0u;
    GpuDeps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    GpuDeps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    GpuDeps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    GpuDeps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    GpuDeps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    GpuDeps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo GpuPassSpec {};
    GpuPassSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    GpuPassSpec.attachmentCount = static_cast<uint32_t>(GpuAtts.size());
    GpuPassSpec.pAttachments    = GpuAtts.data();
    GpuPassSpec.subpassCount    = 1u;
    GpuPassSpec.pSubpasses      = &Subpass;
    GpuPassSpec.dependencyCount = static_cast<uint32_t>(GpuDeps.size());
    GpuPassSpec.pDependencies   = GpuDeps.data();
    if (vkCreateRenderPass(DeviceHandle, &GpuPassSpec, nullptr, &GpuCompositeRenderPass) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 3u> Capacities {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         32u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         32u },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32u },
    }};
    VkDescriptorPoolCreateInfo PoolSpec {};
    PoolSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolSpec.maxSets       = 32u;
    PoolSpec.poolSizeCount = static_cast<uint32_t>(Capacities.size());
    PoolSpec.pPoolSizes    = Capacities.data();
    if (vkCreateDescriptorPool(DeviceHandle, &PoolSpec, nullptr, &OverlayDescriptorAllocator) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    const auto VertPath = CompiledShaderDirectory / "GizmoRasterization.vert.spv";
    const auto FragPath = CompiledShaderDirectory / "GizmoRasterization.frag.spv";
    if (!ConstructGizmoProgram(VertPath, FragPath, true, GizmoDepthProgram) ||
        !ConstructGizmoProgram(VertPath, FragPath, false, GizmoTranslucentProgram))
    {
        TeardownOverlay();
        return false;
    }

    const auto BlitVertPath = CompiledShaderDirectory / "WorldViewportBlit.vert.spv";
    const auto BlitFragPath = CompiledShaderDirectory / "WorldViewportBlit.frag.spv";
    if (std::filesystem::exists(BlitVertPath) && std::filesystem::exists(BlitFragPath))
    {
        if (!ConstructBlitProgram(BlitVertPath, BlitFragPath, ViewportBlitProgram))
        {
            TeardownOverlay();
            return false;
        }
    }

    {
        VkSamplerCreateInfo SampSpec {};
        SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        SampSpec.magFilter    = VK_FILTER_LINEAR;
        SampSpec.minFilter    = VK_FILTER_LINEAR;
        SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.maxLod       = 1.0f;
        if (vkCreateSampler(DeviceHandle, &SampSpec, nullptr, &BlitLinearSampler) != VK_SUCCESS)
        {
            TeardownOverlay();
            return false;
        }
    }

    ReadbackPickPatchBuffer = DeviceInterchange->AllocateBuffer(
        81ull * 16ull,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    UniformBuffer = DeviceInterchange->AllocateBuffer(
        sizeof(GPUWorldGizmoUniformBlock),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (UniformBuffer.BufferHandle == VK_NULL_HANDLE || UniformBuffer.HostAddress == nullptr)
    {
        TeardownOverlay();
        return false;
    }

    Ready = true;
    return true;
}

void WorldGizmoOverlayRasterization::FlushPreviousCompositeCmd()
{
    if (PreviousCompositeCmd != VK_NULL_HANDLE && DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        if (PreviousCompositeSignal != 0ull)
        {
            (void)DeviceInterchange->AwaitTimelineTick(PreviousCompositeSignal);
        }
        DeviceInterchange->FreeCommandBuffer(PreviousCompositeCmd);
    }
    PreviousCompositeCmd    = VK_NULL_HANDLE;
    PreviousCompositeSignal = 0ull;
}

void WorldGizmoOverlayRasterization::TeardownOverlay()
{
    Ready = false;
    CachedPickOrdinals.clear();
    CachedPickPatchW = 0u;
    CachedPickPatchH = 0u;
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
        FlushPreviousCompositeCmd();
        vkDeviceWaitIdle(DeviceHandle);
        ReleaseRenderTargets();
        if (BlitLinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(DeviceHandle, BlitLinearSampler, nullptr);
            BlitLinearSampler = VK_NULL_HANDLE;
        }
        DeviceInterchange->ReleaseBuffer(UniformBuffer);
        DeviceInterchange->ReleaseBuffer(VertexBuffer);
        DeviceInterchange->ReleaseBuffer(UploadReadbackRgbaBuffer);
        DeviceInterchange->ReleaseBuffer(ReadbackPickBuffer);
        DeviceInterchange->ReleaseBuffer(ReadbackPickPatchBuffer);
        DestroyProgramUnit(DeviceHandle, GizmoDepthProgram);
        DestroyProgramUnit(DeviceHandle, GizmoTranslucentProgram);
        DestroyProgramUnit(DeviceHandle, ViewportBlitProgram);
        if (OverlayDescriptorAllocator != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(DeviceHandle, OverlayDescriptorAllocator, nullptr);
            OverlayDescriptorAllocator = VK_NULL_HANDLE;
        }
        if (GpuCompositeRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, GpuCompositeRenderPass, nullptr);
            GpuCompositeRenderPass = VK_NULL_HANDLE;
        }
        if (OverlayRenderTargetFormat != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, OverlayRenderTargetFormat, nullptr);
            OverlayRenderTargetFormat = VK_NULL_HANDLE;
        }
    }
    DeviceInterchange = nullptr;
}

void WorldGizmoOverlayRasterization::ReleaseRenderTargets()
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    if (GpuCompositeFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(DeviceHandle, GpuCompositeFramebuffer, nullptr);
        GpuCompositeFramebuffer = VK_NULL_HANDLE;
    }
    if (TargetFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(DeviceHandle, TargetFramebuffer, nullptr);
        TargetFramebuffer = VK_NULL_HANDLE;
    }
    DeviceInterchange->ReleaseImage2D(ColorAttachment);
    DeviceInterchange->ReleaseImage2D(PickAttachment);
    DeviceInterchange->ReleaseImage2D(DepthAttachment);
    TargetWidthPx  = 0u;
    TargetHeightPx = 0u;
}

bool WorldGizmoOverlayRasterization::EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u || DeviceInterchange == nullptr)
    {
        return false;
    }
    if (TargetFramebuffer != VK_NULL_HANDLE && GpuCompositeFramebuffer != VK_NULL_HANDLE &&
        TargetWidthPx == WidthPx && TargetHeightPx == HeightPx)
    {
        return true;
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    FlushPreviousCompositeCmd();
    vkDeviceWaitIdle(DeviceHandle);
    ReleaseRenderTargets();

    ColorAttachment = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    PickAttachment = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    DepthAttachment = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    if (ColorAttachment.ViewHandle == VK_NULL_HANDLE ||
        PickAttachment.ViewHandle == VK_NULL_HANDLE ||
        DepthAttachment.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseRenderTargets();
        return false;
    }

    const std::array<VkImageView, 3u> Views {{
        ColorAttachment.ViewHandle,
        PickAttachment.ViewHandle,
        DepthAttachment.ViewHandle,
    }};
    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = OverlayRenderTargetFormat;
    FbSpec.attachmentCount = static_cast<uint32_t>(Views.size());
    FbSpec.pAttachments    = Views.data();
    FbSpec.width           = WidthPx;
    FbSpec.height          = HeightPx;
    FbSpec.layers          = 1u;
    if (vkCreateFramebuffer(DeviceHandle, &FbSpec, nullptr, &TargetFramebuffer) != VK_SUCCESS)
    {
        ReleaseRenderTargets();
        return false;
    }

    FbSpec.renderPass = GpuCompositeRenderPass;
    if (vkCreateFramebuffer(DeviceHandle, &FbSpec, nullptr, &GpuCompositeFramebuffer) != VK_SUCCESS)
    {
        ReleaseRenderTargets();
        return false;
    }

    ++ColorAttachmentGeneration;
    TargetWidthPx  = WidthPx;
    TargetHeightPx = HeightPx;
    return true;
}

bool WorldGizmoOverlayRasterization::ConstructGizmoProgram(
    const std::filesystem::path& VertSpirv,
    const std::filesystem::path& FragSpirv,
    bool                         DepthTestWrite,
    GraphicsProgramUnit&         OutUnit)
{
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    OutUnit.VertexSpirvBinary    = DeviceInterchange->LoadShaderBinary(VertSpirv);
    OutUnit.FragmentSpirvBinary    = DeviceInterchange->LoadShaderBinary(FragSpirv);
    if (OutUnit.VertexSpirvBinary == VK_NULL_HANDLE || OutUnit.FragmentSpirvBinary == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 2u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};
    VkDescriptorSetLayoutCreateInfo SetSpec {};
    SetSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetSpec.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetSpec.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(DeviceHandle, &SetSpec, nullptr, &OutUnit.SlotLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo LayoutSpec {};
    LayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutSpec.setLayoutCount = 1u;
    LayoutSpec.pSetLayouts    = &OutUnit.SlotLayout;
    if (vkCreatePipelineLayout(DeviceHandle, &LayoutSpec, nullptr, &OutUnit.ProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> ShaderStages {};
    ShaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderStages[0].module = OutUnit.VertexSpirvBinary;
    ShaderStages[0].pName  = "VertexEntry";
    ShaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderStages[1].module = OutUnit.FragmentSpirvBinary;
    ShaderStages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo   VertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0u, 0u, nullptr, 0u, nullptr };
    VkPipelineInputAssemblyStateCreateInfo InputAssembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0u, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo      ViewportConfig { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0u, 1u, nullptr, 1u, nullptr };

    VkPipelineRasterizationStateCreateInfo RastSpec {};
    RastSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RastSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RastSpec.cullMode    = VK_CULL_MODE_NONE;
    RastSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RastSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthStencil {};
    DepthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencil.depthTestEnable  = DepthTestWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthWriteEnable = DepthTestWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    BlendAtts[0].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[0].blendEnable         = VK_TRUE;
    BlendAtts[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAtts[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtts[0].colorBlendOp        = VK_BLEND_OP_ADD;
    BlendAtts[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    BlendAtts[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAtts[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    BlendAtts[1].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[1].blendEnable         = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo BlendConfig {};
    BlendConfig.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendConfig.attachmentCount = static_cast<uint32_t>(BlendAtts.size());
    BlendConfig.pAttachments    = BlendAtts.data();

    const std::array<VkDynamicState, 2u> DynEnums {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynConfig {};
    DynConfig.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynConfig.dynamicStateCount = static_cast<uint32_t>(DynEnums.size());
    DynConfig.pDynamicStates    = DynEnums.data();

    VkGraphicsPipelineCreateInfo PipeSpec {};
    PipeSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeSpec.stageCount          = static_cast<uint32_t>(ShaderStages.size());
    PipeSpec.pStages             = ShaderStages.data();
    PipeSpec.pVertexInputState   = &VertexInput;
    PipeSpec.pInputAssemblyState = &InputAssembly;
    PipeSpec.pViewportState      = &ViewportConfig;
    PipeSpec.pRasterizationState = &RastSpec;
    PipeSpec.pMultisampleState   = &Multisample;
    PipeSpec.pDepthStencilState  = &DepthStencil;
    PipeSpec.pColorBlendState    = &BlendConfig;
    PipeSpec.pDynamicState       = &DynConfig;
    PipeSpec.layout              = OutUnit.ProgramLayout;
    PipeSpec.renderPass          = OverlayRenderTargetFormat;
    PipeSpec.subpass             = 0u;

    return vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &PipeSpec, nullptr, &OutUnit.ProgramHandle) == VK_SUCCESS;
}

bool WorldGizmoOverlayRasterization::ConstructBlitProgram(
    const std::filesystem::path& VertSpirv,
    const std::filesystem::path& FragSpirv,
    GraphicsProgramUnit&         OutUnit)
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready() || GpuCompositeRenderPass == VK_NULL_HANDLE)
    {
        return false;
    }
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    OutUnit.VertexSpirvBinary   = DeviceInterchange->LoadShaderBinary(VertSpirv);
    OutUnit.FragmentSpirvBinary = DeviceInterchange->LoadShaderBinary(FragSpirv);
    if (OutUnit.VertexSpirvBinary == VK_NULL_HANDLE || OutUnit.FragmentSpirvBinary == VK_NULL_HANDLE)
    {
        return false;
    }

        const std::array<VkDescriptorSetLayoutBinding, 1u> LayoutBindings {{
        { 0u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};
    VkDescriptorSetLayoutCreateInfo SetLayoutSpec {};
    SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutSpec.bindingCount = static_cast<uint32_t>(LayoutBindings.size());
    SetLayoutSpec.pBindings    = LayoutBindings.data();
    if (vkCreateDescriptorSetLayout(DeviceHandle, &SetLayoutSpec, nullptr, &OutUnit.SlotLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo PipeLayoutSpec {};
    PipeLayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutSpec.setLayoutCount = 1u;
    PipeLayoutSpec.pSetLayouts    = &OutUnit.SlotLayout;
    if (vkCreatePipelineLayout(DeviceHandle, &PipeLayoutSpec, nullptr, &OutUnit.ProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> ShaderStages {};
    ShaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderStages[0].module = OutUnit.VertexSpirvBinary;
    ShaderStages[0].pName  = "VertexEntry";
    ShaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderStages[1].module = OutUnit.FragmentSpirvBinary;
    ShaderStages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo VertexInput {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly {};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportConfig {};
    ViewportConfig.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportConfig.viewportCount = 1u;
    ViewportConfig.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RastSpec {};
    RastSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RastSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RastSpec.cullMode    = VK_CULL_MODE_NONE;
    RastSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RastSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthStencil {};
    DepthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencil.depthTestEnable  = VK_FALSE;
    DepthStencil.depthWriteEnable = VK_FALSE;
    DepthStencil.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    BlendAtts[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[0].blendEnable    = VK_FALSE;
    BlendAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[1].blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo BlendConfig {};
    BlendConfig.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendConfig.attachmentCount = static_cast<uint32_t>(BlendAtts.size());
    BlendConfig.pAttachments    = BlendAtts.data();

    const std::array<VkDynamicState, 2u> DynEnums {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynConfig {};
    DynConfig.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynConfig.dynamicStateCount = static_cast<uint32_t>(DynEnums.size());
    DynConfig.pDynamicStates    = DynEnums.data();

    VkGraphicsPipelineCreateInfo PipeSpec {};
    PipeSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeSpec.stageCount          = static_cast<uint32_t>(ShaderStages.size());
    PipeSpec.pStages             = ShaderStages.data();
    PipeSpec.pVertexInputState   = &VertexInput;
    PipeSpec.pInputAssemblyState = &InputAssembly;
    PipeSpec.pViewportState      = &ViewportConfig;
    PipeSpec.pRasterizationState = &RastSpec;
    PipeSpec.pMultisampleState   = &Multisample;
    PipeSpec.pDepthStencilState  = &DepthStencil;
    PipeSpec.pColorBlendState    = &BlendConfig;
    PipeSpec.pDynamicState       = &DynConfig;
    PipeSpec.layout              = OutUnit.ProgramLayout;
    PipeSpec.renderPass          = GpuCompositeRenderPass;
    PipeSpec.subpass             = 0u;

    return vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &PipeSpec, nullptr, &OutUnit.ProgramHandle) == VK_SUCCESS;
}

bool WorldGizmoOverlayRasterization::RasterizeGizmoOntoWorldBuffer(
    std::vector<uint8_t>&         InOutDisplayRgba8,
    uint32_t                      WidthPx,
    uint32_t                      HeightPx,
    const PerspectiveCameraBasis& Camera,
    const Editor::TransformGizmo& Gizmo3D)
{
    if (!Ready || DeviceInterchange == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }
    const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
    if (InOutDisplayRgba8.size() < PixelCount * 4u)
    {
        return false;
    }

    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeCameraBasis(Camera, Eye, Right, Up, Fwd);

    const std::vector<Editor::GizmoGPUVertex> RawVertices = Gizmo3D.BuildTriangleStream(Right, Up, Fwd);
    if (RawVertices.empty())
    {
        CachedPickOrdinals.assign(PixelCount, 0u);
        CachedPickPatchW = 0u;
        CachedPickPatchH = 0u;
        return true;
    }

    // Partition opaque handles first (depth-tested) followed by translucent plane quads (depth-off blended)
    std::vector<Editor::GizmoGPUVertex> OrderedVertices {};
    OrderedVertices.reserve(RawVertices.size());
    for (std::size_t Tri = 0u; Tri + 2u < RawVertices.size(); Tri += 3u)
    {
        if (RawVertices[Tri].PositionAlpha.W >= 0.95f)
        {
            OrderedVertices.push_back(RawVertices[Tri]);
            OrderedVertices.push_back(RawVertices[Tri + 1u]);
            OrderedVertices.push_back(RawVertices[Tri + 2u]);
        }
    }
    const uint32_t OpaqueVertexCount = static_cast<uint32_t>(OrderedVertices.size());
    for (std::size_t Tri = 0u; Tri + 2u < RawVertices.size(); Tri += 3u)
    {
        if (RawVertices[Tri].PositionAlpha.W < 0.95f)
        {
            OrderedVertices.push_back(RawVertices[Tri]);
            OrderedVertices.push_back(RawVertices[Tri + 1u]);
            OrderedVertices.push_back(RawVertices[Tri + 2u]);
        }
    }
    const uint32_t TranslucentVertexCount = static_cast<uint32_t>(OrderedVertices.size()) - OpaqueVertexCount;

    if (!EnsureRenderTargets(WidthPx, HeightPx))
    {
        return false;
    }

    const uint64_t RgbaBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    if (UploadReadbackRgbaBuffer.ByteCapacity < RgbaBytes)
    {
        DeviceInterchange->ReleaseBuffer(UploadReadbackRgbaBuffer);
        UploadReadbackRgbaBuffer = DeviceInterchange->AllocateBuffer(
            RgbaBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }
    const uint64_t PickBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 16ull;
    if (ReadbackPickBuffer.ByteCapacity < PickBytes)
    {
        DeviceInterchange->ReleaseBuffer(ReadbackPickBuffer);
        ReadbackPickBuffer = DeviceInterchange->AllocateBuffer(
            PickBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }
    if (UploadReadbackRgbaBuffer.HostAddress == nullptr || ReadbackPickBuffer.HostAddress == nullptr)
    {
        return false;
    }

    const uint64_t VertBytes = OrderedVertices.size() * sizeof(Editor::GizmoGPUVertex);
    if (VertexBuffer.ByteCapacity < VertBytes)
    {
        DeviceInterchange->ReleaseBuffer(VertexBuffer);
        VertexBuffer = DeviceInterchange->AllocateBuffer(
            VertBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (VertexBuffer.HostAddress == nullptr)
        {
            return false;
        }
    }
    std::memcpy(VertexBuffer.HostAddress, OrderedVertices.data(), static_cast<std::size_t>(VertBytes));

    GPUWorldGizmoUniformBlock Uniforms {};
    const float FovDeg = Camera.VerticalFovRadians * Constants::RadToDegFloat;
    const float Aspect = static_cast<float>(WidthPx) / static_cast<float>(HeightPx);
    const auto  Clip   = Mathematics::ConstructVulkanPerspectiveViewClip(
        Eye,
        Right,
        Up,
        Fwd,
        std::clamp(FovDeg, 10.0f, 140.0f),
        Aspect,
        std::max(0.05f, Camera.NearClipMeters),
        std::max(5000.0f, Camera.FarClipMeters));
    std::memcpy(Uniforms.ViewProjection, Clip.Rows, sizeof(Clip.Rows));
    Uniforms.HoverSpacePacked[0] = static_cast<uint32_t>(Gizmo3D.QueryHoveredHandle());
    Uniforms.HoverSpacePacked[1] = static_cast<uint32_t>(Gizmo3D.QueryActiveHandle());
    Uniforms.HoverSpacePacked[2] = 0u;
    const Mathematics::Axis3D KeyLight = Mathematics::NormalizeAxis(
        Right * 0.38f + Up * 0.45f - Fwd * 0.85f,
        { 0.45f, 0.72f, 0.52f });
    Uniforms.HeadlightDirection[0] = KeyLight.X;
    Uniforms.HeadlightDirection[1] = KeyLight.Y;
    Uniforms.HeadlightDirection[2] = KeyLight.Z;
    Uniforms.HeadlightDirection[3] = 1.0f;
    std::memcpy(UniformBuffer.HostAddress, &Uniforms, sizeof(Uniforms));

    std::memcpy(UploadReadbackRgbaBuffer.HostAddress, InOutDisplayRgba8.data(), PixelCount * 4u);

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    vkResetDescriptorPool(DeviceHandle, OverlayDescriptorAllocator, 0u);

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo SetAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, OverlayDescriptorAllocator, 1u, &GizmoDepthProgram.SlotLayout };
    if (vkAllocateDescriptorSets(DeviceHandle, &SetAlloc, &GizmoSet) != VK_SUCCESS || GizmoSet == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<VkDescriptorBufferInfo, 2u> BufferInfos {{
        { UniformBuffer.BufferHandle, 0u, sizeof(GPUWorldGizmoUniformBlock) },
        { VertexBuffer.BufferHandle, 0u, VertBytes },
    }};
    std::array<VkWriteDescriptorSet, 2u> Writes {};
    for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
    {
        Writes[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Idx].dstSet          = GizmoSet;
        Writes[Idx].dstBinding      = Idx;
        Writes[Idx].descriptorCount = 1u;
        Writes[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Idx].pBufferInfo     = &BufferInfos[Idx];
    }
    vkUpdateDescriptorSets(DeviceHandle, 2u, Writes.data(), 0u, nullptr);

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();

    // 1. Transition ColorAttachment UNDEFINED -> TRANSFER_DST_OPTIMAL, copy DisplayRgba8 in, transition -> COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier ToDst {};
    ToDst.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToDst.srcAccessMask                   = 0u;
    ToDst.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToDst.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    ToDst.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image                           = ColorAttachment.ImageHandle;
    ToDst.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ToDst.subresourceRange.levelCount     = 1u;
    ToDst.subresourceRange.layerCount     = 1u;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToDst);

    VkBufferImageCopy UploadRegion {};
    UploadRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    UploadRegion.imageSubresource.layerCount = 1u;
    UploadRegion.imageExtent                 = { WidthPx, HeightPx, 1u };
    vkCmdCopyBufferToImage(Cmd, UploadReadbackRgbaBuffer.BufferHandle, ColorAttachment.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &UploadRegion);

    VkImageMemoryBarrier ToColor {};
    ToColor.sType                         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToColor.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToColor.dstAccessMask                 = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ToColor.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToColor.newLayout                     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToColor.srcQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
    ToColor.dstQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
    ToColor.image                         = ColorAttachment.ImageHandle;
    ToColor.subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
    ToColor.subresourceRange.levelCount   = 1u;
    ToColor.subresourceRange.layerCount   = 1u;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToColor);

    // 2. Execute GizmoRasterization.slang over the loaded world frame
    std::array<VkClearValue, 3u> Clears {};
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = 0xFFFFFFFFu;
    Clears[1].color.uint32[2] = 0xFFFFFFFFu;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil    = { 1.0f, 0u };

    VkRenderPassBeginInfo PassBegin {};
    PassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    PassBegin.renderPass        = OverlayRenderTargetFormat;
    PassBegin.framebuffer       = TargetFramebuffer;
    PassBegin.renderArea.extent = { WidthPx, HeightPx };
    PassBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    PassBegin.pClearValues      = Clears.data();
    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport VkView { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   VkScissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VkView);
    vkCmdSetScissor(Cmd, 0u, 1u, &VkScissor);

    if (OpaqueVertexCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthProgram.ProgramHandle);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, OpaqueVertexCount, 1u, 0u, 0u);
    }
    if (TranslucentVertexCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoTranslucentProgram.ProgramHandle);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoTranslucentProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, TranslucentVertexCount, 1u, OpaqueVertexCount, 0u);
    }

    vkCmdEndRenderPass(Cmd);

    // 3. Read back composited RGBA8 and GPU PickIDs
    VkImageMemoryBarrier ToRead {};
    ToRead.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToRead.srcAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ToRead.dstAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
    ToRead.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ToRead.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ToRead.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToRead.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToRead.image                       = ColorAttachment.ImageHandle;
    ToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ToRead.subresourceRange.levelCount = 1u;
    ToRead.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToRead);

    vkCmdCopyImageToBuffer(Cmd, ColorAttachment.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, UploadReadbackRgbaBuffer.BufferHandle, 1u, &UploadRegion);
    vkCmdCopyImageToBuffer(Cmd, PickAttachment.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackPickBuffer.BufferHandle, 1u, &UploadRegion);

    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    (void)DeviceInterchange->AwaitTimelineTick(Signal);
    vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);

    std::memcpy(InOutDisplayRgba8.data(), UploadReadbackRgbaBuffer.HostAddress, PixelCount * 4u);

    CachedPickPatchW = 0u;
    CachedPickPatchH = 0u;
    CachedPickOrdinals.resize(PixelCount);
    const auto* RawPickWords = static_cast<const uint32_t*>(ReadbackPickBuffer.HostAddress);
    for (std::size_t Idx = 0u; Idx < PixelCount; ++Idx)
    {
        CachedPickOrdinals[Idx] = RawPickWords[Idx * 4u + 3u];
    }
    return true;
}

bool WorldGizmoOverlayRasterization::CompositeOntoGpuViewport(
    const GpuViewportOverlayRequest& Request,
    GpuViewportOverlayResult&        OutResult)
{
    OutResult = {};
    if (!Ready || DeviceInterchange == nullptr || !DeviceInterchange->Ready() ||
        ViewportBlitProgram.ProgramHandle == VK_NULL_HANDLE || GpuCompositeRenderPass == VK_NULL_HANDLE ||
        Request.WorldSourceView == VK_NULL_HANDLE || Request.TargetWidthPx == 0u || Request.TargetHeightPx == 0u)
    {
        return false;
    }

    FlushPreviousCompositeCmd();

    if (!EnsureRenderTargets(Request.TargetWidthPx, Request.TargetHeightPx))
    {
        return false;
    }

    // Build gizmo geometry if requested
    std::vector<Editor::GizmoGPUVertex> OrderedVertices {};
    uint32_t OpaqueVertexCount      = 0u;
    uint32_t TranslucentVertexCount = 0u;
    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeCameraBasis(Request.Camera, Eye, Right, Up, Fwd);
    if (Request.DrawGizmo && Request.Gizmo3D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> RawVertices = Request.Gizmo3D->BuildTriangleStream(Right, Up, Fwd);
        if (!RawVertices.empty())
        {
            OrderedVertices.reserve(RawVertices.size());
            for (std::size_t Tri = 0u; Tri + 2u < RawVertices.size(); Tri += 3u)
            {
                if (RawVertices[Tri].PositionAlpha.W >= 0.95f)
                {
                    OrderedVertices.push_back(RawVertices[Tri]);
                    OrderedVertices.push_back(RawVertices[Tri + 1u]);
                    OrderedVertices.push_back(RawVertices[Tri + 2u]);
                }
            }
            OpaqueVertexCount = static_cast<uint32_t>(OrderedVertices.size());
            for (std::size_t Tri = 0u; Tri + 2u < RawVertices.size(); Tri += 3u)
            {
                if (RawVertices[Tri].PositionAlpha.W < 0.95f)
                {
                    OrderedVertices.push_back(RawVertices[Tri]);
                    OrderedVertices.push_back(RawVertices[Tri + 1u]);
                    OrderedVertices.push_back(RawVertices[Tri + 2u]);
                }
            }
            TranslucentVertexCount = static_cast<uint32_t>(OrderedVertices.size()) - OpaqueVertexCount;
        }
    }

    uint64_t VertBytes = 0ull;
    if (!OrderedVertices.empty())
    {
        VertBytes = OrderedVertices.size() * sizeof(Editor::GizmoGPUVertex);
        if (VertexBuffer.ByteCapacity < VertBytes)
        {
            DeviceInterchange->ReleaseBuffer(VertexBuffer);
            VertexBuffer = DeviceInterchange->AllocateBuffer(
                std::max<uint64_t>(VertBytes, 16384ull),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (VertexBuffer.HostAddress == nullptr)
            {
                return false;
            }
        }
        std::memcpy(VertexBuffer.HostAddress, OrderedVertices.data(), static_cast<std::size_t>(VertBytes));

        GPUWorldGizmoUniformBlock Uniforms {};
        const float FovDeg = Request.Camera.VerticalFovRadians * Constants::RadToDegFloat;
        const float Aspect = static_cast<float>(Request.TargetWidthPx) / static_cast<float>(Request.TargetHeightPx);
        const auto  Clip   = Mathematics::ConstructVulkanPerspectiveViewClip(
            Eye,
            Right,
            Up,
            Fwd,
            std::clamp(FovDeg, 10.0f, 140.0f),
            Aspect,
            std::max(0.05f, Request.Camera.NearClipMeters),
            std::max(5000.0f, Request.Camera.FarClipMeters));
        std::memcpy(Uniforms.ViewProjection, Clip.Rows, sizeof(Clip.Rows));
        Uniforms.HoverSpacePacked[0] = static_cast<uint32_t>(Request.Gizmo3D->QueryHoveredHandle());
        Uniforms.HoverSpacePacked[1] = static_cast<uint32_t>(Request.Gizmo3D->QueryActiveHandle());
        Uniforms.HoverSpacePacked[2] = 0u;
        const Mathematics::Axis3D KeyLight = Mathematics::NormalizeAxis(
            Right * 0.38f + Up * 0.45f - Fwd * 0.85f,
            { 0.45f, 0.72f, 0.52f });
        Uniforms.HeadlightDirection[0] = KeyLight.X;
        Uniforms.HeadlightDirection[1] = KeyLight.Y;
        Uniforms.HeadlightDirection[2] = KeyLight.Z;
        Uniforms.HeadlightDirection[3] = 1.0f;
        std::memcpy(UniformBuffer.HostAddress, &Uniforms, sizeof(Uniforms));
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    vkResetDescriptorPool(DeviceHandle, OverlayDescriptorAllocator, 0u);

    VkDescriptorSet BlitSet = VK_NULL_HANDLE;
    {
        VkDescriptorSetAllocateInfo AllocBlit { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, OverlayDescriptorAllocator, 1u, &ViewportBlitProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &AllocBlit, &BlitSet) != VK_SUCCESS || BlitSet == VK_NULL_HANDLE)
        {
            return false;
        }
        const VkDescriptorImageInfo ImgInfo { BlitLinearSampler, Request.WorldSourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet BlitWrite {};
        BlitWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        BlitWrite.dstSet          = BlitSet;
        BlitWrite.dstBinding      = 0u;
        BlitWrite.descriptorCount = 1u;
        BlitWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        BlitWrite.pImageInfo      = &ImgInfo;
        vkUpdateDescriptorSets(DeviceHandle, 1u, &BlitWrite, 0u, nullptr);
    }

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (!OrderedVertices.empty())
    {
        VkDescriptorSetAllocateInfo AllocGizmo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, OverlayDescriptorAllocator, 1u, &GizmoDepthProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &AllocGizmo, &GizmoSet) != VK_SUCCESS || GizmoSet == VK_NULL_HANDLE)
        {
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 2u> BufferInfos {{
            { UniformBuffer.BufferHandle, 0u, sizeof(GPUWorldGizmoUniformBlock) },
            { VertexBuffer.BufferHandle, 0u, VertBytes },
        }};
        std::array<VkWriteDescriptorSet, 2u> GizmoWrites {};
        for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
        {
            GizmoWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            GizmoWrites[Idx].dstSet          = GizmoSet;
            GizmoWrites[Idx].dstBinding      = Idx;
            GizmoWrites[Idx].descriptorCount = 1u;
            GizmoWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            GizmoWrites[Idx].pBufferInfo     = &BufferInfos[Idx];
        }
        vkUpdateDescriptorSets(DeviceHandle, static_cast<uint32_t>(GizmoWrites.size()), GizmoWrites.data(), 0u, nullptr);
    }

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    std::array<VkClearValue, 3u> Clears {};
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = 0xFFFFFFFFu;
    Clears[1].color.uint32[2] = 0xFFFFFFFFu;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil    = { 1.0f, 0u };

    VkRenderPassBeginInfo PassBegin {};
    PassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    PassBegin.renderPass        = GpuCompositeRenderPass;
    PassBegin.framebuffer       = GpuCompositeFramebuffer;
    PassBegin.renderArea.extent = { Request.TargetWidthPx, Request.TargetHeightPx };
    PassBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    PassBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport Vp { 0.0f, 0.0f, static_cast<float>(Request.TargetWidthPx), static_cast<float>(Request.TargetHeightPx), 0.0f, 1.0f };
    const VkRect2D Sc { { 0, 0 }, { Request.TargetWidthPx, Request.TargetHeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &Vp);
    vkCmdSetScissor(Cmd, 0u, 1u, &Sc);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ViewportBlitProgram.ProgramHandle);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ViewportBlitProgram.ProgramLayout, 0u, 1u, &BlitSet, 0u, nullptr);
    vkCmdDraw(Cmd, 3u, 1u, 0u, 0u);

    if (!OrderedVertices.empty())
    {
        if (OpaqueVertexCount > 0u)
        {
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthProgram.ProgramHandle);
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
            vkCmdDraw(Cmd, OpaqueVertexCount, 1u, 0u, 0u);
        }
        if (TranslucentVertexCount > 0u)
        {
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoTranslucentProgram.ProgramHandle);
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoTranslucentProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
            vkCmdDraw(Cmd, TranslucentVertexCount, 1u, OpaqueVertexCount, 0u);
        }
    }

    vkCmdEndRenderPass(Cmd);

    const bool DoPatchReadback = Request.RequestPickPatch && !OrderedVertices.empty() && ReadbackPickPatchBuffer.HostAddress != nullptr;
    int32_t PatchX0 = 0;
    int32_t PatchY0 = 0;
    uint32_t PatchW = 0u;
    uint32_t PatchH = 0u;

    if (DoPatchReadback)
    {
        const int32_t Cx = std::clamp(static_cast<int32_t>(Request.PickCursorX), 0, static_cast<int32_t>(Request.TargetWidthPx) - 1);
        const int32_t Cy = std::clamp(static_cast<int32_t>(Request.PickCursorY), 0, static_cast<int32_t>(Request.TargetHeightPx) - 1);
        PatchX0 = std::max(0, Cx - 4);
        PatchY0 = std::max(0, Cy - 4);
        const int32_t PatchX1 = std::min(static_cast<int32_t>(Request.TargetWidthPx) - 1, Cx + 4);
        const int32_t PatchY1 = std::min(static_cast<int32_t>(Request.TargetHeightPx) - 1, Cy + 4);
        PatchW = static_cast<uint32_t>(PatchX1 - PatchX0 + 1);
        PatchH = static_cast<uint32_t>(PatchY1 - PatchY0 + 1);

        VkBufferImageCopy PatchCopy {};
        PatchCopy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        PatchCopy.imageOffset      = { PatchX0, PatchY0, 0 };
        PatchCopy.imageExtent      = { PatchW, PatchH, 1u };
        vkCmdCopyImageToBuffer(Cmd, PickAttachment.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackPickPatchBuffer.BufferHandle, 1u, &PatchCopy);

        const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
        (void)DeviceInterchange->AwaitTimelineTick(Signal);
        DeviceInterchange->FreeCommandBuffer(Cmd);

        CachedPickPatchX0 = static_cast<uint32_t>(PatchX0);
        CachedPickPatchY0 = static_cast<uint32_t>(PatchY0);
        CachedPickPatchW  = PatchW;
        CachedPickPatchH  = PatchH;
        const std::size_t PatchPixels = static_cast<std::size_t>(PatchW) * PatchH;
        CachedPickOrdinals.resize(PatchPixels);
        const auto* RawPickWords = static_cast<const uint32_t*>(ReadbackPickPatchBuffer.HostAddress);
        for (std::size_t Idx = 0u; Idx < PatchPixels; ++Idx)
        {
            CachedPickOrdinals[Idx] = RawPickWords[Idx * 4u + 3u];
        }
    }
    else
    {
        if (!Request.DrawGizmo || OrderedVertices.empty())
        {
            CachedPickPatchW = 0u;
            CachedPickPatchH = 0u;
            CachedPickOrdinals.clear();
        }
        PreviousCompositeSignal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
        PreviousCompositeCmd    = Cmd;
    }

    OutResult.ColorImageView  = ColorAttachment.ViewHandle;
    OutResult.ImageGeneration = ColorAttachmentGeneration;
    OutResult.WidthPx         = Request.TargetWidthPx;
    OutResult.HeightPx        = Request.TargetHeightPx;
    return true;
}

Editor::GizmoHandleOrdinal WorldGizmoOverlayRasterization::QueryLastGpuPickAtPixel(uint32_t PixelX, uint32_t PixelY) const noexcept
{
    if (TargetWidthPx == 0u || TargetHeightPx == 0u)
    {
        return Editor::GizmoHandleOrdinal::None;
    }
    const uint32_t ClampedX = std::min(PixelX, TargetWidthPx - 1u);
    const uint32_t ClampedY = std::min(PixelY, TargetHeightPx - 1u);

    uint32_t BestOrd  = 0u;
    int32_t  BestDist = 9999;

    if (CachedPickPatchW > 0u && CachedPickPatchH > 0u &&
        CachedPickOrdinals.size() >= static_cast<std::size_t>(CachedPickPatchW) * CachedPickPatchH)
    {
        for (uint32_t Py = 0u; Py < CachedPickPatchH; ++Py)
        {
            const int32_t Sy = CachedPickPatchY0 + static_cast<int32_t>(Py);
            const int32_t Dy = Sy - static_cast<int32_t>(ClampedY);
            for (uint32_t Px = 0u; Px < CachedPickPatchW; ++Px)
            {
                const int32_t Sx = CachedPickPatchX0 + static_cast<int32_t>(Px);
                const int32_t Dx = Sx - static_cast<int32_t>(ClampedX);
                const uint32_t Ord = CachedPickOrdinals[static_cast<std::size_t>(Py) * CachedPickPatchW + static_cast<std::size_t>(Px)];
                const int32_t Dist = Dx * Dx + Dy * Dy;
                if (Ord != 0u && Ord <= static_cast<uint32_t>(Editor::GizmoHandleOrdinal::CenterRing) && Dist < BestDist)
                {
                    BestOrd  = Ord;
                    BestDist = Dist;
                }
            }
        }
        return static_cast<Editor::GizmoHandleOrdinal>(BestOrd);
    }

    if (CachedPickOrdinals.size() < static_cast<std::size_t>(TargetWidthPx) * TargetHeightPx)
    {
        return Editor::GizmoHandleOrdinal::None;
    }

    // Search a 9x9 pixel neighborhood around cursor for crisp axis/arc usability
    for (int32_t Dy = -4; Dy <= 4; ++Dy)
    {
        const int32_t Sy = std::clamp(static_cast<int32_t>(ClampedY) + Dy, 0, static_cast<int32_t>(TargetHeightPx) - 1);
        for (int32_t Dx = -4; Dx <= 4; ++Dx)
        {
            const int32_t Sx  = std::clamp(static_cast<int32_t>(ClampedX) + Dx, 0, static_cast<int32_t>(TargetWidthPx) - 1);
            const uint32_t Ord = CachedPickOrdinals[static_cast<std::size_t>(Sy) * TargetWidthPx + static_cast<std::size_t>(Sx)];
            const int32_t Dist = Dx * Dx + Dy * Dy;
            if (Ord != 0u && Ord <= static_cast<uint32_t>(Editor::GizmoHandleOrdinal::CenterRing) && Dist < BestDist)
            {
                BestOrd  = Ord;
                BestDist = Dist;
            }
        }
    }
    return static_cast<Editor::GizmoHandleOrdinal>(BestOrd);
}

} // namespace Frontier::GeometricRaster
