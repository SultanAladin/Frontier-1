/*============================================================================================================================================
                                                       IMGUIOVERLAYRASTERIZATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ImGuiOverlayRasterization driving ImGuiOverlayRasterization.slang over OS VkSwapchainKHR or headless targets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"

#if !defined(__ANDROID__)
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{

[[nodiscard]] ImTextureID EncodeImGuiTextureId(VkDescriptorSet Descriptor) noexcept
{
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(Descriptor));
}

} // namespace

namespace Frontier::GeometricRaster
{

namespace
{

struct ImGuiShaderVertex
{
    float PosX;
    float PosY;
    float UvX;
    float UvY;
    float Red;
    float Green;
    float Blue;
    float Alpha;
};

struct ImGuiPushConstants
{
    float ScaleX;
    float ScaleY;
    float TranslateX;
    float TranslateY;
};

[[nodiscard]] uint64_t HashPixelBytesSampled(const uint8_t* Bytes, std::size_t TotalBytes) noexcept
{
    if (Bytes == nullptr || TotalBytes == 0u)
    {
        return 0ull;
    }
    uint64_t Hash = 14695981039346656037ull;
    std::size_t Idx = 0u;
    for (; Idx + sizeof(uint64_t) <= TotalBytes; Idx += sizeof(uint64_t))
    {
        uint64_t Word = 0ull;
        std::memcpy(&Word, Bytes + Idx, sizeof(Word));
        Hash ^= Word;
        Hash *= 1099511628211ull;
    }
    for (; Idx < TotalBytes; ++Idx)
    {
        Hash ^= static_cast<uint64_t>(Bytes[Idx]);
        Hash *= 1099511628211ull;
    }
    Hash ^= static_cast<uint64_t>(TotalBytes);
    return (Hash == 0ull) ? 1ull : Hash;
}

} // namespace

ImGuiOverlayRasterization::~ImGuiOverlayRasterization()
{
    TeardownOverlay();
}

bool ImGuiOverlayRasterization::InitializeOverlay(
    VulkanInterchange*              TargetDevice,
    const std::filesystem::path&      CompiledShaderDir,
    Windowing::DesktopPlatformWindow* OptionalDesktopWindow,
    uint32_t                          InitialWidthPx,
    uint32_t                          InitialHeightPx,
    std::string*                      OptionalDiagnostic)
{
    TeardownOverlay();
    if (TargetDevice == nullptr || !TargetDevice->Ready())
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "ImGuiOverlayRasterization requires an initialized VulkanInterchange.";
        }
        return false;
    }

    BoundDevice     = TargetDevice;
    DesktopWindow   = OptionalDesktopWindow;
    ShaderDirectory = CompiledShaderDir;
    const VkDevice LogicalVk = BoundDevice->AccessDevice();

#if !defined(__ANDROID__)
    if (DesktopWindow != nullptr &&
        DesktopWindow->PlatformCategory() != Windowing::WindowPlatformCategory::GlfwNull &&
        DesktopWindow->PlatformCategory() != Windowing::WindowPlatformCategory::HeadlessStub &&
        DesktopWindow->NativeGlfwHandle() != nullptr &&
        vkDestroySurfaceKHR != nullptr &&
        vkCreateSwapchainKHR != nullptr)
    {
        auto* GlfwWin = static_cast<GLFWwindow*>(DesktopWindow->NativeGlfwHandle());
        if (glfwCreateWindowSurface(BoundDevice->AccessInstance(), GlfwWin, nullptr, &WindowSurface) == VK_SUCCESS)
        {
            VkBool32 PresentSupported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR != nullptr)
            {
                vkGetPhysicalDeviceSurfaceSupportKHR(
                    BoundDevice->AccessPhysicalDevice(),
                    BoundDevice->QueryQueueOrdinal(),
                    WindowSurface,
                    &PresentSupported);
            }
            if (PresentSupported == VK_TRUE)
            {
                Stats.LiveWindowSwapchain = true;
            }
            else
            {
                vkDestroySurfaceKHR(BoundDevice->AccessInstance(), WindowSurface, nullptr);
                WindowSurface = VK_NULL_HANDLE;
            }
        }
    }
#endif

    ActiveColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    if (Stats.LiveWindowSwapchain && vkGetPhysicalDeviceSurfaceFormatsKHR != nullptr)
    {
        uint32_t FormatCount = 0u;
        vkGetPhysicalDeviceSurfaceFormatsKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &FormatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> Formats(FormatCount);
        if (FormatCount > 0u)
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &FormatCount, Formats.data());
            ActiveColorFormat = Formats.front().format;
            for (const auto& Candidate : Formats)
            {
                if (Candidate.format == VK_FORMAT_B8G8R8A8_UNORM || Candidate.format == VK_FORMAT_R8G8B8A8_UNORM)
                {
                    ActiveColorFormat = Candidate.format;
                    break;
                }
            }
        }
    }

    VkSemaphoreCreateInfo SemSpec {};
    SemSpec.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (Stats.LiveWindowSwapchain)
    {
        (void)vkCreateSemaphore(LogicalVk, &SemSpec, nullptr, &ImageAcquireSemaphore);
        (void)vkCreateSemaphore(LogicalVk, &SemSpec, nullptr, &RenderDoneSemaphore);
    }

    if (!ConstructRasterResources(ActiveColorFormat, OptionalDiagnostic))
    {
        TeardownOverlay();
        return false;
    }

    if (!RebuildSwapchainOrOffscreenTarget(std::max(64u, InitialWidthPx), std::max(64u, InitialHeightPx)))
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Failed to initialize ImGuiOverlayRasterization target attachments.";
        }
        TeardownOverlay();
        return false;
    }

    if (!InitializeUpstreamImGuiBackend(OptionalDiagnostic))
    {
        TeardownOverlay();
        return false;
    }

    return true;
}

bool ImGuiOverlayRasterization::InitializeUpstreamImGuiBackend(std::string* OptionalDiagnostic)
{
    if (UpstreamImGuiBackendReady)
    {
        return true;
    }
    if (ImGui::GetCurrentContext() == nullptr || BoundDevice == nullptr || OverlayRenderUnit == VK_NULL_HANDLE)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Dear ImGui and the Vulkan render pass must exist before its Vulkan renderer is initialized.";
        }
        return false;
    }

    ImGui_ImplVulkan_InitInfo InitInfo {};
    InitInfo.ApiVersion                   = VK_API_VERSION_1_2;
    InitInfo.Instance                     = BoundDevice->AccessInstance();
    InitInfo.PhysicalDevice               = BoundDevice->AccessPhysicalDevice();
    InitInfo.Device                       = BoundDevice->AccessDevice();
    InitInfo.QueueFamily                  = BoundDevice->QueryQueueOrdinal();
    InitInfo.Queue                        = BoundDevice->AccessQueue();
    InitInfo.DescriptorPoolSize           = 512u;
    InitInfo.MinImageCount                = 2u;
    InitInfo.ImageCount                   = std::max(2u, static_cast<uint32_t>(SwapchainImages.size()));
    InitInfo.PipelineInfoMain.RenderPass  = OverlayRenderUnit;
    InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&InitInfo))
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Dear ImGui's upstream Vulkan renderer rejected the Frontier Vulkan presentation configuration.";
        }
        return false;
    }

    UpstreamImGuiBackendReady = true;
    return true;
}

void ImGuiOverlayRasterization::TeardownUpstreamImGuiBackend() noexcept
{
    if (UpstreamImGuiBackendReady)
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplVulkan_Shutdown();
        }
        UpstreamImGuiBackendReady = false;
    }
}

void ImGuiOverlayRasterization::PrepareImGuiFrame() noexcept
{
    if (UpstreamImGuiBackendReady && ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplVulkan_NewFrame();
    }
}

bool ImGuiOverlayRasterization::ConstructRasterResources(VkFormat TargetFormat, std::string* OptionalDiagnostic)
{
    const VkDevice LogicalVk = BoundDevice->AccessDevice();

    VkSamplerCreateInfo SampSpec {};
    SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SampSpec.magFilter    = VK_FILTER_LINEAR;
    SampSpec.minFilter    = VK_FILTER_LINEAR;
    SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(LogicalVk, &SampSpec, nullptr, &LinearSampler) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorSetLayoutBinding Slots[2]{};
    Slots[0].binding         = 0u;
    Slots[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Slots[0].descriptorCount = 1u;
    Slots[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    Slots[1].binding         = 1u;
    Slots[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Slots[1].descriptorCount = 1u;
    Slots[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SlotLayoutSpec {};
    SlotLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SlotLayoutSpec.bindingCount = 2u;
    SlotLayoutSpec.pBindings    = Slots;
    if (vkCreateDescriptorSetLayout(LogicalVk, &SlotLayoutSpec, nullptr, &DescriptorLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorPoolSize AllocSizes[2]{};
    AllocSizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256u };
    AllocSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256u };

    VkDescriptorPoolCreateInfo AllocSpec {};
    AllocSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    AllocSpec.maxSets       = 256u;
    AllocSpec.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    AllocSpec.poolSizeCount = 2u;
    AllocSpec.pPoolSizes    = AllocSizes;
    if (vkCreateDescriptorPool(LogicalVk, &AllocSpec, nullptr, &DescriptorAllocator) != VK_SUCCESS)
    {
        return false;
    }

    VkPushConstantRange PushRange {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(ImGuiPushConstants);

    VkPipelineLayoutCreateInfo LayoutCreate {};
    LayoutCreate.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutCreate.setLayoutCount         = 1u;
    LayoutCreate.pSetLayouts            = &DescriptorLayout;
    LayoutCreate.pushConstantRangeCount = 1u;
    LayoutCreate.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(LogicalVk, &LayoutCreate, nullptr, &RasterLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkAttachmentDescription ColorAttach {};
    ColorAttach.format         = TargetFormat;
    ColorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    ColorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ColorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ColorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    ColorAttach.finalLayout    = Stats.LiveWindowSwapchain
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference ColorRef {};
    ColorRef.attachment = 0u;
    ColorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subunit {};
    Subunit.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subunit.colorAttachmentCount = 1u;
    Subunit.pColorAttachments    = &ColorRef;

    VkRenderPassCreateInfo TargetSpec {};
    TargetSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    TargetSpec.attachmentCount = 1u;
    TargetSpec.pAttachments    = &ColorAttach;
    TargetSpec.subpassCount    = 1u;
    TargetSpec.pSubpasses      = &Subunit;
    if (vkCreateRenderPass(LogicalVk, &TargetSpec, nullptr, &OverlayRenderUnit) != VK_SUCCESS)
    {
        return false;
    }

    const VkShaderModule VertMod = BoundDevice->LoadShaderBinary(ShaderDirectory / "ImGuiOverlayRasterization.vert.spv");
    const VkShaderModule FragMod = BoundDevice->LoadShaderBinary(ShaderDirectory / "ImGuiOverlayRasterization.frag.spv");
    if (VertMod == VK_NULL_HANDLE || FragMod == VK_NULL_HANDLE)
    {
        if (VertMod != VK_NULL_HANDLE) vkDestroyShaderModule(LogicalVk, VertMod, nullptr);
        if (FragMod != VK_NULL_HANDLE) vkDestroyShaderModule(LogicalVk, FragMod, nullptr);
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Missing ImGuiOverlayRasterization.{vert,frag}.spv shader files.";
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo ShaderUnits[2]{};
    ShaderUnits[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderUnits[0].module = VertMod;
    ShaderUnits[0].pName  = "VertexEntry";

    ShaderUnits[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderUnits[1].module = FragMod;
    ShaderUnits[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo   VertexInput {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputTopo {};
    InputTopo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputTopo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportSpec {};
    ViewportSpec.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportSpec.viewportCount = 1u;
    ViewportSpec.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RasterSpec {};
    RasterSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RasterSpec.cullMode    = VK_CULL_MODE_NONE;
    RasterSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RasterSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState BlendAttach {};
    BlendAttach.blendEnable         = VK_TRUE;
    BlendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
    BlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    BlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;
    BlendAttach.colorWriteMask      =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColorBlend {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = 1u;
    ColorBlend.pAttachments    = &BlendAttach;

    const VkDynamicState DynamicOpts[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicSpec {};
    DynamicSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicSpec.dynamicStateCount = 2u;
    DynamicSpec.pDynamicStates    = DynamicOpts;

    VkGraphicsPipelineCreateInfo UnitCreate {};
    UnitCreate.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    UnitCreate.stageCount          = 2u;
    UnitCreate.pStages             = ShaderUnits;
    UnitCreate.pVertexInputState   = &VertexInput;
    UnitCreate.pInputAssemblyState = &InputTopo;
    UnitCreate.pViewportState      = &ViewportSpec;
    UnitCreate.pRasterizationState = &RasterSpec;
    UnitCreate.pMultisampleState   = &Multisample;
    UnitCreate.pColorBlendState    = &ColorBlend;
    UnitCreate.pDynamicState       = &DynamicSpec;
    UnitCreate.layout              = RasterLayout;
    UnitCreate.renderPass          = OverlayRenderUnit;

    const VkResult CreateStatus = vkCreateGraphicsPipelines(LogicalVk, VK_NULL_HANDLE, 1u, &UnitCreate, nullptr, &OverlayRasterUnit);
    vkDestroyShaderModule(LogicalVk, VertMod, nullptr);
    vkDestroyShaderModule(LogicalVk, FragMod, nullptr);
    if (CreateStatus != VK_SUCCESS)
    {
        return false;
    }

    VertexStreamBuffer = BoundDevice->AllocateBuffer(
        524288ull * sizeof(ImGuiShaderVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    UploadStagingBuffer = BoundDevice->AllocateBuffer(
        16ull * 1024ull * 1024ull,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    FallbackWhiteTex = BoundDevice->AllocateImage2D(
        1u,
        1u,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    if (UploadStagingBuffer.HostAddress != nullptr && FallbackWhiteTex.ImageHandle != VK_NULL_HANDLE)
    {
        const uint32_t WhitePixel = 0xFFFFFFFFu;
        std::memcpy(UploadStagingBuffer.HostAddress, &WhitePixel, sizeof(uint32_t));

        VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
        VkImageMemoryBarrier ToDst {};
        ToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToDst.image               = FallbackWhiteTex.ImageHandle;
        ToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        ToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToDst);

        VkBufferImageCopy CopyRegion {};
        CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        CopyRegion.imageExtent      = { 1u, 1u, 1u };
        vkCmdCopyBufferToImage(Cmd, UploadStagingBuffer.BufferHandle, FallbackWhiteTex.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);

        VkImageMemoryBarrier ToRead = ToDst;
        ToRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ToRead.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
        ToRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToRead);

        const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
        (void)BoundDevice->AwaitTimelineTick(SyncPoint);
    }

    VkDescriptorSetAllocateInfo SetAlloc {};
    SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAlloc.descriptorPool     = DescriptorAllocator;
    SetAlloc.descriptorSetCount = 1u;
    SetAlloc.pSetLayouts        = &DescriptorLayout;
    if (vkAllocateDescriptorSets(LogicalVk, &SetAlloc, &FallbackDescriptor) == VK_SUCCESS)
    {
        VkDescriptorBufferInfo BufSpec { VertexStreamBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorImageInfo  ImgSpec { LinearSampler, FallbackWhiteTex.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkWriteDescriptorSet Writes[2]{};
        Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[0].dstSet          = FallbackDescriptor;
        Writes[0].dstBinding      = 0u;
        Writes[0].descriptorCount = 1u;
        Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[0].pBufferInfo     = &BufSpec;

        Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[1].dstSet          = FallbackDescriptor;
        Writes[1].dstBinding      = 1u;
        Writes[1].descriptorCount = 1u;
        Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Writes[1].pImageInfo      = &ImgSpec;

        vkUpdateDescriptorSets(LogicalVk, 2u, Writes, 0u, nullptr);
    }

    return VertexStreamBuffer.BufferHandle != VK_NULL_HANDLE && FallbackDescriptor != VK_NULL_HANDLE;
}

void ImGuiOverlayRasterization::DestroyTargetAttachments() noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    const VkDevice LogicalVk = BoundDevice->AccessDevice();
    vkDeviceWaitIdle(LogicalVk);

    for (VkFramebuffer TargetFb : TargetAttachments)
    {
        if (TargetFb != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(LogicalVk, TargetFb, nullptr);
        }
    }
    TargetAttachments.clear();

    for (VkImageView View : SwapchainViews)
    {
        if (View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(LogicalVk, View, nullptr);
        }
    }
    SwapchainViews.clear();
    SwapchainImages.clear();

    if (SwapchainHandle != VK_NULL_HANDLE && vkDestroySwapchainKHR != nullptr)
    {
        vkDestroySwapchainKHR(LogicalVk, SwapchainHandle, nullptr);
        SwapchainHandle = VK_NULL_HANDLE;
    }

    BoundDevice->ReleaseImage2D(OffscreenTarget);
}

bool ImGuiOverlayRasterization::RebuildSwapchainOrOffscreenTarget(uint32_t WidthPx, uint32_t HeightPx)
{
    DestroyTargetAttachments();
    const VkDevice LogicalVk = BoundDevice->AccessDevice();
    WidthPx                  = std::max(16u, WidthPx);
    HeightPx                 = std::max(16u, HeightPx);

    if (Stats.LiveWindowSwapchain && WindowSurface != VK_NULL_HANDLE)
    {
        VkSurfaceCapabilitiesKHR Caps {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &Caps);
        if (Caps.currentExtent.width != 0xFFFFFFFFu && Caps.currentExtent.width > 0u)
        {
            WidthPx  = Caps.currentExtent.width;
            HeightPx = Caps.currentExtent.height;
        }
        else
        {
            WidthPx  = std::clamp(WidthPx, Caps.minImageExtent.width, Caps.maxImageExtent.width);
            HeightPx = std::clamp(HeightPx, Caps.minImageExtent.height, Caps.maxImageExtent.height);
        }

        uint32_t ImageCount = Caps.minImageCount + 1u;
        if (Caps.maxImageCount > 0u && ImageCount > Caps.maxImageCount)
        {
            ImageCount = Caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ScSpec {};
        ScSpec.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ScSpec.surface          = WindowSurface;
        ScSpec.minImageCount    = ImageCount;
        ScSpec.imageFormat      = ActiveColorFormat;
        ScSpec.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        ScSpec.imageExtent      = { WidthPx, HeightPx };
        ScSpec.imageArrayLayers = 1u;
        ScSpec.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            ((Caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                 ? static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                 : static_cast<VkImageUsageFlags>(0u));
        ScSpec.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ScSpec.preTransform     = Caps.currentTransform;
        ScSpec.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ScSpec.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        ScSpec.clipped          = VK_TRUE;

        if (vkCreateSwapchainKHR(LogicalVk, &ScSpec, nullptr, &SwapchainHandle) != VK_SUCCESS)
        {
            return false;
        }

        uint32_t RetrievedCount = 0u;
        vkGetSwapchainImagesKHR(LogicalVk, SwapchainHandle, &RetrievedCount, nullptr);
        SwapchainImages.resize(RetrievedCount);
        vkGetSwapchainImagesKHR(LogicalVk, SwapchainHandle, &RetrievedCount, SwapchainImages.data());

        SwapchainViews.resize(RetrievedCount, VK_NULL_HANDLE);
        TargetAttachments.resize(RetrievedCount, VK_NULL_HANDLE);
        for (uint32_t Idx = 0u; Idx < RetrievedCount; ++Idx)
        {
            VkImageViewCreateInfo ViewSpec {};
            ViewSpec.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ViewSpec.image            = SwapchainImages[Idx];
            ViewSpec.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            ViewSpec.format           = ActiveColorFormat;
            ViewSpec.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            if (vkCreateImageView(LogicalVk, &ViewSpec, nullptr, &SwapchainViews[Idx]) != VK_SUCCESS)
            {
                return false;
            }

            VkFramebufferCreateInfo FbSpec {};
            FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            FbSpec.renderPass      = OverlayRenderUnit;
            FbSpec.attachmentCount = 1u;
            FbSpec.pAttachments    = &SwapchainViews[Idx];
            FbSpec.width           = WidthPx;
            FbSpec.height          = HeightPx;
            FbSpec.layers          = 1u;
            if (vkCreateFramebuffer(LogicalVk, &FbSpec, nullptr, &TargetAttachments[Idx]) != VK_SUCCESS)
            {
                return false;
            }
        }
    }
    else
    {
        OffscreenTarget = BoundDevice->AllocateImage2D(
            WidthPx,
            HeightPx,
            ActiveColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (OffscreenTarget.ImageHandle == VK_NULL_HANDLE)
        {
            return false;
        }

        VkFramebufferCreateInfo FbSpec {};
        FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FbSpec.renderPass      = OverlayRenderUnit;
        FbSpec.attachmentCount = 1u;
        FbSpec.pAttachments    = &OffscreenTarget.ViewHandle;
        FbSpec.width           = WidthPx;
        FbSpec.height          = HeightPx;
        FbSpec.layers          = 1u;

        VkFramebuffer OffscreenFb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(LogicalVk, &FbSpec, nullptr, &OffscreenFb) != VK_SUCCESS)
        {
            return false;
        }
        TargetAttachments.push_back(OffscreenFb);
    }

    Stats.WidthPx  = WidthPx;
    Stats.HeightPx = HeightPx;
    return true;
}

VkDescriptorSet ImGuiOverlayRasterization::EnsureTextureUploaded(ImTextureData* SourceTex, bool FontAtlasTexture)
{
    if (SourceTex == nullptr)
    {
        return FallbackDescriptor;
    }

    if (SourceTex->Status == ImTextureStatus_WantDestroy || SourceTex->Status == ImTextureStatus_Destroyed)
    {
        const auto Existing = TextureCache.find(SourceTex);
        if (Existing != TextureCache.end())
        {
            if (Existing->second.Descriptor != VK_NULL_HANDLE)
            {
                (void)vkFreeDescriptorSets(BoundDevice->AccessDevice(), DescriptorAllocator, 1u, &Existing->second.Descriptor);
            }
            BoundDevice->ReleaseImage2D(Existing->second.Image);
            TextureCache.erase(Existing);
        }
        SourceTex->SetTexID(ImTextureID_Invalid);
        SourceTex->SetStatus(ImTextureStatus_Destroyed);
        Stats.ResidentTextureCount = static_cast<uint32_t>(TextureCache.size());
        return FallbackDescriptor;
    }

    if (SourceTex->Pixels == nullptr || SourceTex->Width <= 0 || SourceTex->Height <= 0)
    {
        return FallbackDescriptor;
    }

    const uint32_t    W        = static_cast<uint32_t>(SourceTex->Width);
    const uint32_t    H        = static_cast<uint32_t>(SourceTex->Height);
    const bool        Rgba32   = (SourceTex->Format == ImTextureFormat_RGBA32);
    const std::size_t ByteLen  = static_cast<std::size_t>(W) * H * (Rgba32 ? 4u : 1u);
    const auto*       RawBytes = static_cast<const uint8_t*>(SourceTex->Pixels);
    const bool        TextureRequestsUpload =
        SourceTex->Status == ImTextureStatus_WantCreate || SourceTex->Status == ImTextureStatus_WantUpdates;

    auto FoundEntry = TextureCache.find(SourceTex);
    if (FoundEntry == TextureCache.end() && TextureCache.size() >= 255u)
    {
        auto Oldest = TextureCache.end();
        for (auto It = TextureCache.begin(); It != TextureCache.end(); ++It)
        {
            if (It->second.LastPresentSerial == TexturePresentSerial)
            {
                continue;
            }
            if (Oldest == TextureCache.end() || It->second.LastTouched < Oldest->second.LastTouched)
            {
                Oldest = It;
            }
        }
        if (Oldest == TextureCache.end())
        {
            return FallbackDescriptor;
        }
        if (Oldest != TextureCache.end())
        {
            if (Oldest->second.Descriptor != VK_NULL_HANDLE)
            {
                (void)vkFreeDescriptorSets(BoundDevice->AccessDevice(), DescriptorAllocator, 1u, &Oldest->second.Descriptor);
            }
            BoundDevice->ReleaseImage2D(Oldest->second.Image);
            TextureCache.erase(Oldest);
        }
    }
    if (FoundEntry == TextureCache.end())
    {
        FoundEntry = TextureCache.try_emplace(SourceTex).first;
    }
    auto& Entry = FoundEntry->second;
    Entry.LastTouched = ++TextureUseCounter;
    Entry.LastPresentSerial = TexturePresentSerial;
    const uint64_t Hash = FontAtlasTexture && !TextureRequestsUpload
        ? ((Entry.ContentHash != 0ull && Entry.Image.WidthPx == W && Entry.Image.HeightPx == H) ? Entry.ContentHash : 1ull)
        : HashPixelBytesSampled(RawBytes, ByteLen);

    if (!TextureRequestsUpload &&
        Entry.Image.ImageHandle != VK_NULL_HANDLE &&
        Entry.Image.WidthPx == W &&
        Entry.Image.HeightPx == H &&
        Entry.ContentHash == Hash &&
        Entry.Descriptor != VK_NULL_HANDLE)
    {
        SourceTex->SetTexID(EncodeImGuiTextureId(Entry.Descriptor));
        SourceTex->SetStatus(ImTextureStatus_OK);
        return Entry.Descriptor;
    }

    const VkDevice LogicalVk   = BoundDevice->AccessDevice();
    const uint64_t UploadBytes = static_cast<uint64_t>(W) * H * 4ull;
    if (UploadBytes > UploadStagingBuffer.ByteCapacity)
    {
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        UploadStagingBuffer = BoundDevice->AllocateBuffer(
            UploadBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (UploadStagingBuffer.HostAddress == nullptr)
    {
        return FallbackDescriptor;
    }

    if (Rgba32)
    {
        std::memcpy(UploadStagingBuffer.HostAddress, RawBytes, static_cast<std::size_t>(UploadBytes));
    }
    else
    {
        auto* Dst = static_cast<uint8_t*>(UploadStagingBuffer.HostAddress);
        for (std::size_t Px = 0u; Px < static_cast<std::size_t>(W) * H; ++Px)
        {
            Dst[Px * 4u + 0u] = 255u;
            Dst[Px * 4u + 1u] = 255u;
            Dst[Px * 4u + 2u] = 255u;
            Dst[Px * 4u + 3u] = RawBytes[Px];
        }
    }

    const bool ReusesResidentImage =
        Entry.Image.ImageHandle != VK_NULL_HANDLE && Entry.Image.WidthPx == W && Entry.Image.HeightPx == H;
    if (!ReusesResidentImage)
    {
        BoundDevice->ReleaseImage2D(Entry.Image);
        Entry.Image = BoundDevice->AllocateImage2D(
            W,
            H,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (Entry.Image.ImageHandle == VK_NULL_HANDLE)
    {
        return FallbackDescriptor;
    }

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return FallbackDescriptor;
    }
    VkImageMemoryBarrier ToDst {};
    ToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToDst.oldLayout           = ReusesResidentImage ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    ToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image               = Entry.Image.ImageHandle;
    ToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    ToDst.srcAccessMask       = ReusesResidentImage
        ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT)
        : static_cast<VkAccessFlags>(0u);
    ToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        ReusesResidentImage ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &ToDst);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { W, H, 1u };
    vkCmdCopyBufferToImage(Cmd, UploadStagingBuffer.BufferHandle, Entry.Image.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);

    VkImageMemoryBarrier ToRead = ToDst;
    ToRead.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToRead.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToRead.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToRead.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToRead);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncPoint))
    {
        return FallbackDescriptor;
    }

    if (Entry.Descriptor == VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo AllocSpec {};
        AllocSpec.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocSpec.descriptorPool     = DescriptorAllocator;
        AllocSpec.descriptorSetCount = 1u;
        AllocSpec.pSetLayouts        = &DescriptorLayout;
        if (vkAllocateDescriptorSets(LogicalVk, &AllocSpec, &Entry.Descriptor) != VK_SUCCESS)
        {
            return FallbackDescriptor;
        }
    }

    VkDescriptorBufferInfo BufSpec { VertexStreamBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
    VkDescriptorImageInfo  ImgSpec { LinearSampler, Entry.Image.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet Writes[2]{};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Entry.Descriptor;
    Writes[0].dstBinding      = 0u;
    Writes[0].descriptorCount = 1u;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[0].pBufferInfo     = &BufSpec;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Entry.Descriptor;
    Writes[1].dstBinding      = 1u;
    Writes[1].descriptorCount = 1u;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[1].pImageInfo      = &ImgSpec;

    vkUpdateDescriptorSets(LogicalVk, 2u, Writes, 0u, nullptr);
    Entry.ContentHash = Hash;
    SourceTex->SetTexID(EncodeImGuiTextureId(Entry.Descriptor));
    SourceTex->SetStatus(ImTextureStatus_OK);
    Stats.ResidentTextureCount = static_cast<uint32_t>(TextureCache.size());
    ++Stats.TextureUploadCount;
    return Entry.Descriptor;
}

bool ImGuiOverlayRasterization::PresentImGuiBatches(
    const ImDrawData* OverlayGeometry,
    uint32_t          TargetWidthPx,
    uint32_t          TargetHeightPx)
{
    const auto ReportPresentationFailure = [this](std::string DiagnosticText)
    {
        LastPresentationDiagnostic = std::move(DiagnosticText);
        return false;
    };

    if (BoundDevice == nullptr || !BoundDevice->Ready() || OverlayRenderUnit == VK_NULL_HANDLE ||
        !UpstreamImGuiBackendReady)
    {
        return ReportPresentationFailure("The ImGui Vulkan presentation backend is not initialized.");
    }

    TargetWidthPx  = std::max(16u, TargetWidthPx);
    TargetHeightPx = std::max(16u, TargetHeightPx);
    if (TargetWidthPx != Stats.WidthPx || TargetHeightPx != Stats.HeightPx || TargetAttachments.empty())
    {
        if (!RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx))
        {
            return ReportPresentationFailure("Unable to rebuild the Vulkan presentation attachments.");
        }
    }

    // Texture uploads submit their own short-lived transfer command buffer.  Complete those
    // requests before acquiring an image or beginning the render pass: doing it inside the
    // old custom command decoder allowed a texture upload to race the frame being presented.
    uint32_t TextureRequests = 0u;
    if (OverlayGeometry != nullptr && OverlayGeometry->Textures != nullptr)
    {
        for (ImTextureData* RequestedTexture : *OverlayGeometry->Textures)
        {
            if (RequestedTexture != nullptr && RequestedTexture->Status != ImTextureStatus_OK)
            {
                const bool IsUploadRequest =
                    RequestedTexture->Status == ImTextureStatus_WantCreate ||
                    RequestedTexture->Status == ImTextureStatus_WantUpdates;
                ImGui_ImplVulkan_UpdateTexture(RequestedTexture);
                TextureRequests += IsUploadRequest ? 1u : 0u;
            }
        }
    }

    // Frontier also uses ImTextureData for viewport and thumbnail textures. Unlike
    // font-atlas textures, these application-owned records are not automatically in
    // ImDrawData::Textures, so explicitly service those referenced by draw commands.
    if (OverlayGeometry != nullptr)
    {
        for (int ListIndex = 0; ListIndex < OverlayGeometry->CmdListsCount; ++ListIndex)
        {
            const ImDrawList* DrawList = OverlayGeometry->CmdLists[ListIndex];
            for (int CommandIndex = 0; CommandIndex < DrawList->CmdBuffer.Size; ++CommandIndex)
            {
                ImTextureData* Texture = DrawList->CmdBuffer[CommandIndex].TexRef._TexData;
                if (Texture == nullptr)
                {
                    continue;
                }
                if (Texture->Status == ImTextureStatus_OK && Texture->GetTexID() == ImTextureID_Invalid &&
                    Texture->BackendUserData == nullptr)
                {
                    // Recover older Frontier producers that marked CPU pixels as uploaded
                    // without ever giving the renderer a GPU texture handle.
                    Texture->SetStatus(ImTextureStatus_WantCreate);
                }
                if (Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates ||
                    Texture->Status == ImTextureStatus_WantDestroy)
                {
                    const bool IsUploadRequest =
                        Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates;
                    ImGui_ImplVulkan_UpdateTexture(Texture);
                    TextureRequests += IsUploadRequest ? 1u : 0u;
                }
                if (Texture->GetTexID() == ImTextureID_Invalid)
                {
                    return ReportPresentationFailure("An ImGui draw command references a texture that the Vulkan backend did not create.");
                }
            }
        }
    }

    uint32_t DecodedCommandCount = 0u;
    uint32_t DecodedIndexCount   = 0u;
    if (OverlayGeometry != nullptr)
    {
        for (int ListIndex = 0; ListIndex < OverlayGeometry->CmdListsCount; ++ListIndex)
        {
            const ImDrawList* DrawList = OverlayGeometry->CmdLists[ListIndex];
            for (int CommandIndex = 0; CommandIndex < DrawList->CmdBuffer.Size; ++CommandIndex)
            {
                const ImDrawCmd& Command = DrawList->CmdBuffer[CommandIndex];
                if (Command.UserCallback == nullptr && Command.ElemCount > 0u)
                {
                    ++DecodedCommandCount;
                    DecodedIndexCount += Command.ElemCount;
                }
            }
        }
    }

    uint32_t ImageIndex = 0u;
    if (Stats.LiveWindowSwapchain)
    {
        const VkResult AcquireResult = vkAcquireNextImageKHR(
            BoundDevice->AccessDevice(),
            SwapchainHandle,
            1000000000ull,
            ImageAcquireSemaphore,
            VK_NULL_HANDLE,
            &ImageIndex);
        if (AcquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            if (!RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx))
            {
                return ReportPresentationFailure("The window swapchain became out of date and could not be rebuilt.");
            }
            LastPresentationDiagnostic.clear();
            return true;
        }
        if (AcquireResult != VK_SUCCESS && AcquireResult != VK_SUBOPTIMAL_KHR)
        {
            return ReportPresentationFailure(
                "vkAcquireNextImageKHR failed with VkResult " + std::to_string(static_cast<int32_t>(AcquireResult)) + ".");
        }
    }

    VkCommandBuffer CommandBuffer = BoundDevice->BeginSingleTimeCommands();
    if (CommandBuffer == VK_NULL_HANDLE)
    {
        return ReportPresentationFailure("Unable to allocate a Vulkan command buffer for the ImGui frame.");
    }

    VkClearValue ClearColor {};
    ClearColor.color = { { 0.055f, 0.055f, 0.063f, 1.0f } };
    VkRenderPassBeginInfo RenderPassBegin {};
    RenderPassBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassBegin.renderPass      = OverlayRenderUnit;
    RenderPassBegin.framebuffer     = TargetAttachments[ImageIndex];
    RenderPassBegin.renderArea      = { { 0, 0 }, { Stats.WidthPx, Stats.HeightPx } };
    RenderPassBegin.clearValueCount = 1u;
    RenderPassBegin.pClearValues    = &ClearColor;

    vkCmdBeginRenderPass(CommandBuffer, &RenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
    if (OverlayGeometry != nullptr)
    {
        // This is the exact supported draw path used by the older, physically proven
        // Frontier presenter.  It owns indexed command offsets, clip rectangles, render
        // state resets, font atlas descriptors, and texture update semantics.
        ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(OverlayGeometry), CommandBuffer);
    }
    vkCmdEndRenderPass(CommandBuffer);

    if (Stats.LiveWindowSwapchain)
    {
        const VkResult EndResult = vkEndCommandBuffer(CommandBuffer);
        if (EndResult != VK_SUCCESS)
        {
            vkFreeCommandBuffers(BoundDevice->AccessDevice(), BoundDevice->AccessCommandAllocator(), 1u, &CommandBuffer);
            return ReportPresentationFailure(
                "vkEndCommandBuffer failed with VkResult " + std::to_string(static_cast<int32_t>(EndResult)) + ".");
        }

        const VkPipelineStageFlags WaitMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo SubmitInfo {};
        SubmitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.waitSemaphoreCount   = 1u;
        SubmitInfo.pWaitSemaphores      = &ImageAcquireSemaphore;
        SubmitInfo.pWaitDstStageMask    = &WaitMask;
        SubmitInfo.commandBufferCount   = 1u;
        SubmitInfo.pCommandBuffers      = &CommandBuffer;
        SubmitInfo.signalSemaphoreCount = 1u;
        SubmitInfo.pSignalSemaphores    = &RenderDoneSemaphore;
        const VkResult SubmitResult = vkQueueSubmit(BoundDevice->AccessQueue(), 1u, &SubmitInfo, VK_NULL_HANDLE);
        if (SubmitResult != VK_SUCCESS)
        {
            vkFreeCommandBuffers(BoundDevice->AccessDevice(), BoundDevice->AccessCommandAllocator(), 1u, &CommandBuffer);
            return ReportPresentationFailure(
                "vkQueueSubmit failed with VkResult " + std::to_string(static_cast<int32_t>(SubmitResult)) + ".");
        }

        VkPresentInfoKHR PresentInfo {};
        PresentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.waitSemaphoreCount = 1u;
        PresentInfo.pWaitSemaphores    = &RenderDoneSemaphore;
        PresentInfo.swapchainCount     = 1u;
        PresentInfo.pSwapchains        = &SwapchainHandle;
        PresentInfo.pImageIndices      = &ImageIndex;
        const VkResult PresentResult = vkQueuePresentKHR(BoundDevice->AccessQueue(), &PresentInfo);

        // The presenter intentionally serializes its single reusable command allocator.
        // This mirrors the old working frame lifetime and prevents an in-flight command
        // buffer from being freed/reused before the next acquire.
        const VkResult IdleResult = vkQueueWaitIdle(BoundDevice->AccessQueue());
        vkFreeCommandBuffers(BoundDevice->AccessDevice(), BoundDevice->AccessCommandAllocator(), 1u, &CommandBuffer);
        if (IdleResult != VK_SUCCESS)
        {
            return ReportPresentationFailure(
                "vkQueueWaitIdle failed with VkResult " + std::to_string(static_cast<int32_t>(IdleResult)) + ".");
        }
        if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR || PresentResult == VK_SUBOPTIMAL_KHR)
        {
            if (!RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx))
            {
                return ReportPresentationFailure("The window swapchain became unavailable after presentation and could not be rebuilt.");
            }
        }
        else if (PresentResult != VK_SUCCESS)
        {
            return ReportPresentationFailure(
                "vkQueuePresentKHR failed with VkResult " + std::to_string(static_cast<int32_t>(PresentResult)) + ".");
        }
    }
    else
    {
        const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(CommandBuffer);
        if (!BoundDevice->AwaitTimelineTick(SyncPoint))
        {
            return ReportPresentationFailure("The offscreen Vulkan frame did not reach its timeline synchronization point.");
        }
    }

    Stats.LastDrawCommandCount = DecodedCommandCount;
    Stats.LastDrawVertexCount  = DecodedIndexCount;
    Stats.ResidentTextureCount = OverlayGeometry != nullptr && OverlayGeometry->Textures != nullptr
        ? static_cast<uint32_t>(OverlayGeometry->Textures->Size)
        : 0u;
    Stats.TextureUploadCount += TextureRequests;
    LastPresentationDiagnostic.clear();
    ++Stats.PresentedBatchCount;
    return true;
}

bool ImGuiOverlayRasterization::ReadbackLastPresentedRgba(
    std::vector<uint8_t>& OutRgbaPixels,
    uint32_t&             OutWidthPx,
    uint32_t&             OutHeightPx)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || Stats.WidthPx == 0u || Stats.HeightPx == 0u)
    {
        return false;
    }

    VkImage SourceImage = OffscreenTarget.ImageHandle;
    if (Stats.LiveWindowSwapchain && !SwapchainImages.empty())
    {
        SourceImage = SwapchainImages.front();
    }
    if (SourceImage == VK_NULL_HANDLE)
    {
        return false;
    }

    const uint64_t TotalBytes = static_cast<uint64_t>(Stats.WidthPx) * Stats.HeightPx * 4ull;
    if (TotalBytes > UploadStagingBuffer.ByteCapacity)
    {
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        UploadStagingBuffer = BoundDevice->AllocateBuffer(
            TotalBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (UploadStagingBuffer.HostAddress == nullptr)
    {
        return false;
    }

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Stats.LiveWindowSwapchain)
    {
        VkImageMemoryBarrier ToSrc {};
        ToSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ToSrc.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ToSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToSrc.image               = SourceImage;
        ToSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        ToSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToSrc);
    }

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { Stats.WidthPx, Stats.HeightPx, 1u };
    vkCmdCopyImageToBuffer(Cmd, SourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, UploadStagingBuffer.BufferHandle, 1u, &CopyRegion);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncPoint))
    {
        return false;
    }

    OutWidthPx  = Stats.WidthPx;
    OutHeightPx = Stats.HeightPx;
    OutRgbaPixels.resize(static_cast<std::size_t>(TotalBytes));
    std::memcpy(OutRgbaPixels.data(), UploadStagingBuffer.HostAddress, static_cast<std::size_t>(TotalBytes));

    if (ActiveColorFormat == VK_FORMAT_B8G8R8A8_UNORM)
    {
        for (std::size_t Off = 0u; Off + 3u < OutRgbaPixels.size(); Off += 4u)
        {
            std::swap(OutRgbaPixels[Off + 0u], OutRgbaPixels[Off + 2u]);
        }
    }
    return true;
}

void ImGuiOverlayRasterization::TeardownOverlay() noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        UpstreamImGuiBackendReady = false;
        BoundDevice = nullptr;
        return;
    }

    TeardownUpstreamImGuiBackend();
    DestroyTargetAttachments();
    const VkDevice LogicalVk = BoundDevice->AccessDevice();

    for (auto& [Key, Entry] : TextureCache)
    {
        (void)Key;
        BoundDevice->ReleaseImage2D(Entry.Image);
    }
    TextureCache.clear();
    TextureUseCounter = 0ull;
    TexturePresentSerial = 0ull;

    BoundDevice->ReleaseImage2D(FallbackWhiteTex);
    BoundDevice->ReleaseBuffer(VertexStreamBuffer);
    BoundDevice->ReleaseBuffer(UploadStagingBuffer);

    if (OverlayRasterUnit != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(LogicalVk, OverlayRasterUnit, nullptr);
        OverlayRasterUnit = VK_NULL_HANDLE;
    }
    if (RasterLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(LogicalVk, RasterLayout, nullptr);
        RasterLayout = VK_NULL_HANDLE;
    }
    if (OverlayRenderUnit != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(LogicalVk, OverlayRenderUnit, nullptr);
        OverlayRenderUnit = VK_NULL_HANDLE;
    }
    if (DescriptorAllocator != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(LogicalVk, DescriptorAllocator, nullptr);
        DescriptorAllocator = VK_NULL_HANDLE;
        FallbackDescriptor  = VK_NULL_HANDLE;
    }
    if (DescriptorLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(LogicalVk, DescriptorLayout, nullptr);
        DescriptorLayout = VK_NULL_HANDLE;
    }
    if (LinearSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(LogicalVk, LinearSampler, nullptr);
        LinearSampler = VK_NULL_HANDLE;
    }
    if (ImageAcquireSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(LogicalVk, ImageAcquireSemaphore, nullptr);
        ImageAcquireSemaphore = VK_NULL_HANDLE;
    }
    if (RenderDoneSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(LogicalVk, RenderDoneSemaphore, nullptr);
        RenderDoneSemaphore = VK_NULL_HANDLE;
    }
    if (WindowSurface != VK_NULL_HANDLE && vkDestroySurfaceKHR != nullptr)
    {
        vkDestroySurfaceKHR(BoundDevice->AccessInstance(), WindowSurface, nullptr);
        WindowSurface = VK_NULL_HANDLE;
    }

    Stats       = {};
    BoundDevice = nullptr;
}

} // namespace Frontier::GeometricRaster
