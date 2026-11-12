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
#include <cstring>
#include <limits>

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

    return true;
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

VkDescriptorSet ImGuiOverlayRasterization::EnsureTextureUploaded(const ImTextureData* SourceTex, bool FontAtlasTexture)
{
    if (SourceTex == nullptr || SourceTex->Pixels == nullptr || SourceTex->Width <= 0 || SourceTex->Height <= 0)
    {
        return FallbackDescriptor;
    }

    const uint32_t    W        = static_cast<uint32_t>(SourceTex->Width);
    const uint32_t    H        = static_cast<uint32_t>(SourceTex->Height);
    const bool        Rgba32   = (SourceTex->Format == ImTextureFormat_RGBA32);
    const std::size_t ByteLen  = static_cast<std::size_t>(W) * H * (Rgba32 ? 4u : 1u);
    const auto*       RawBytes = static_cast<const uint8_t*>(SourceTex->Pixels);

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
    const uint64_t Hash  = FontAtlasTexture
        ? ((Entry.ContentHash != 0ull && Entry.Image.WidthPx == W && Entry.Image.HeightPx == H) ? Entry.ContentHash : 1ull)
        : HashPixelBytesSampled(RawBytes, ByteLen);

    if (Entry.Image.ImageHandle != VK_NULL_HANDLE &&
        Entry.Image.WidthPx == W &&
        Entry.Image.HeightPx == H &&
        Entry.ContentHash == Hash &&
        Entry.Descriptor != VK_NULL_HANDLE)
    {
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

    if (Entry.Image.ImageHandle == VK_NULL_HANDLE || Entry.Image.WidthPx != W || Entry.Image.HeightPx != H)
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
    VkImageMemoryBarrier ToDst {};
    ToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image               = Entry.Image.ImageHandle;
    ToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    ToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToDst);

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
    (void)BoundDevice->AwaitTimelineTick(SyncPoint);

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
    return Entry.Descriptor;
}

bool ImGuiOverlayRasterization::PresentImGuiBatches(
    const ImDrawData* OverlayGeometry,
    uint32_t          TargetWidthPx,
    uint32_t          TargetHeightPx)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || OverlayRasterUnit == VK_NULL_HANDLE)
    {
        return false;
    }

    ++TexturePresentSerial;
    if (TexturePresentSerial == 0ull)
    {
        TexturePresentSerial = 1ull;
        for (auto& [TextureKey, TextureEntry] : TextureCache)
        {
            (void)TextureKey;
            TextureEntry.LastPresentSerial = 0ull;
        }
    }
    TargetWidthPx  = std::max(16u, TargetWidthPx);
    TargetHeightPx = std::max(16u, TargetHeightPx);
    if (TargetWidthPx != Stats.WidthPx || TargetHeightPx != Stats.HeightPx || TargetAttachments.empty())
    {
        if (!RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx))
        {
            return false;
        }
    }

    struct PreparedBatchSlice
    {
        uint32_t        FirstVertex = 0u;
        uint32_t        VertexCount = 0u;
        VkRect2D        Scissor     {};
        VkDescriptorSet SetHandle   = VK_NULL_HANDLE;
    };
    std::vector<PreparedBatchSlice> Batches {};

    const ImFontAtlas*   FontAtlas = (ImGui::GetCurrentContext() != nullptr) ? ImGui::GetIO().Fonts : nullptr;
    const ImTextureData* FontTex   = (FontAtlas != nullptr) ? FontAtlas->TexData : nullptr;

    const uint64_t RequiredVertexCount = (OverlayGeometry != nullptr && OverlayGeometry->TotalVtxCount > 0)
        ? static_cast<uint64_t>(OverlayGeometry->TotalVtxCount)
        : 0ull;
    const uint64_t RequiredVertexBytes = RequiredVertexCount * sizeof(ImGuiShaderVertex);
    if (RequiredVertexBytes > VertexStreamBuffer.ByteCapacity)
    {
        const uint64_t NewCapacity = std::max<uint64_t>(RequiredVertexBytes, VertexStreamBuffer.ByteCapacity * 2ull);
        if (NewCapacity > static_cast<uint64_t>(std::numeric_limits<VkDeviceSize>::max()))
        {
            return false;
        }
        (void)vkDeviceWaitIdle(BoundDevice->AccessDevice());
        BoundDevice->ReleaseBuffer(VertexStreamBuffer);
        VertexStreamBuffer = BoundDevice->AllocateBuffer(
            NewCapacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (VertexStreamBuffer.BufferHandle == VK_NULL_HANDLE || VertexStreamBuffer.HostAddress == nullptr)
        {
            return false;
        }
        VkDescriptorBufferInfo BufferInfo { VertexStreamBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE };
        auto RefreshVertexStreamDescriptor = [&](VkDescriptorSet Set)
        {
            if (Set != VK_NULL_HANDLE)
            {
                VkWriteDescriptorSet BufferWrite {};
                BufferWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                BufferWrite.dstSet          = Set;
                BufferWrite.dstBinding      = 0u;
                BufferWrite.descriptorCount = 1u;
                BufferWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                BufferWrite.pBufferInfo     = &BufferInfo;
                vkUpdateDescriptorSets(BoundDevice->AccessDevice(), 1u, &BufferWrite, 0u, nullptr);
            }
        };
        RefreshVertexStreamDescriptor(FallbackDescriptor);
        for (auto& [TextureKey, TextureEntry] : TextureCache)
        {
            (void)TextureKey;
            RefreshVertexStreamDescriptor(TextureEntry.Descriptor);
        }
    }

    uint32_t       WrittenVertices = 0u;
    const uint32_t MaxVertices     = static_cast<uint32_t>(std::min<uint64_t>(
        VertexStreamBuffer.ByteCapacity / sizeof(ImGuiShaderVertex), std::numeric_limits<uint32_t>::max()));
    auto*          OutVerts        = static_cast<ImGuiShaderVertex*>(VertexStreamBuffer.HostAddress);

    if (OverlayGeometry != nullptr && OutVerts != nullptr)
    {
        const float OffX = OverlayGeometry->DisplayPos.x;
        const float OffY = OverlayGeometry->DisplayPos.y;

        for (int ListIdx = 0; ListIdx < OverlayGeometry->CmdListsCount; ++ListIdx)
        {
            const ImDrawList* CmdList = OverlayGeometry->CmdLists[ListIdx];
            const ImDrawVert* VtxArr  = CmdList->VtxBuffer.Data;
            const ImDrawIdx*  IdxArr  = CmdList->IdxBuffer.Data;

            for (int CmdIdx = 0; CmdIdx < CmdList->CmdBuffer.Size; ++CmdIdx)
            {
                const ImDrawCmd& Cmd = CmdList->CmdBuffer[CmdIdx];
                if (Cmd.UserCallback != nullptr)
                {
                    Cmd.UserCallback(CmdList, &Cmd);
                    continue;
                }

                const int32_t ClipX0 = std::max(0, static_cast<int32_t>(std::floor(Cmd.ClipRect.x - OffX)));
                const int32_t ClipY0 = std::max(0, static_cast<int32_t>(std::floor(Cmd.ClipRect.y - OffY)));
                const int32_t ClipX1 = std::min(static_cast<int32_t>(Stats.WidthPx), static_cast<int32_t>(std::ceil(Cmd.ClipRect.z - OffX)));
                const int32_t ClipY1 = std::min(static_cast<int32_t>(Stats.HeightPx), static_cast<int32_t>(std::ceil(Cmd.ClipRect.w - OffY)));
                if (ClipX0 >= ClipX1 || ClipY0 >= ClipY1 || Cmd.ElemCount == 0u)
                {
                    continue;
                }

                const ImTextureData* TexSource = Cmd.TexRef._TexData != nullptr ? Cmd.TexRef._TexData : FontTex;
                const VkDescriptorSet BoundSet = EnsureTextureUploaded(TexSource, TexSource == FontTex);

                const uint32_t StartVert = WrittenVertices;
                for (uint32_t Elem = 0u; Elem < Cmd.ElemCount && WrittenVertices < MaxVertices; ++Elem)
                {
                    const ImDrawVert& Src = VtxArr[Cmd.VtxOffset + IdxArr[Cmd.IdxOffset + Elem]];
                    const uint32_t    Col = Src.col;
                    OutVerts[WrittenVertices++] = ImGuiShaderVertex{
                        Src.pos.x - OffX,
                        Src.pos.y - OffY,
                        Src.uv.x,
                        Src.uv.y,
                        static_cast<float>((Col >> IM_COL32_R_SHIFT) & 0xFFu) / 255.0f,
                        static_cast<float>((Col >> IM_COL32_G_SHIFT) & 0xFFu) / 255.0f,
                        static_cast<float>((Col >> IM_COL32_B_SHIFT) & 0xFFu) / 255.0f,
                        static_cast<float>((Col >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f,
                    };
                }

                if (WrittenVertices > StartVert)
                {
                    PreparedBatchSlice Batch {};
                    Batch.FirstVertex = StartVert;
                    Batch.VertexCount = WrittenVertices - StartVert;
                    Batch.Scissor     = {
                        { ClipX0, ClipY0 },
                        { static_cast<uint32_t>(ClipX1 - ClipX0), static_cast<uint32_t>(ClipY1 - ClipY0) },
                    };
                    Batch.SetHandle   = BoundSet;
                    Batches.push_back(Batch);
                }
            }
        }
    }

    uint32_t ImageIndex = 0u;
    if (Stats.LiveWindowSwapchain)
    {
        const VkResult Acq = vkAcquireNextImageKHR(
            BoundDevice->AccessDevice(),
            SwapchainHandle,
            1000000000ull,
            ImageAcquireSemaphore,
            VK_NULL_HANDLE,
            &ImageIndex);
        if (Acq == VK_ERROR_OUT_OF_DATE_KHR)
        {
            (void)RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx);
            return true;
        }
        if (Acq != VK_SUCCESS && Acq != VK_SUBOPTIMAL_KHR)
        {
            return false;
        }
    }

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    VkClearValue    ClearColor {};
    ClearColor.color = { { 0.055f, 0.055f, 0.063f, 1.0f } };

    VkRenderPassBeginInfo RpBegin {};
    RpBegin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass      = OverlayRenderUnit;
    RpBegin.framebuffer     = TargetAttachments[ImageIndex];
    RpBegin.renderArea      = { { 0, 0 }, { Stats.WidthPx, Stats.HeightPx } };
    RpBegin.clearValueCount = 1u;
    RpBegin.pClearValues    = &ClearColor;

    vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, OverlayRasterUnit);

    VkViewport Vp {};
    Vp.x        = 0.0f;
    Vp.y        = 0.0f;
    Vp.width    = static_cast<float>(Stats.WidthPx);
    Vp.height   = static_cast<float>(Stats.HeightPx);
    Vp.minDepth = 0.0f;
    Vp.maxDepth = 1.0f;
    vkCmdSetViewport(Cmd, 0u, 1u, &Vp);

    const ImGuiPushConstants Push {
        2.0f / static_cast<float>(Stats.WidthPx),
        2.0f / static_cast<float>(Stats.HeightPx),
        -1.0f,
        -1.0f,
    };
    vkCmdPushConstants(Cmd, RasterLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(Push), &Push);

    for (const auto& Batch : Batches)
    {
        vkCmdSetScissor(Cmd, 0u, 1u, &Batch.Scissor);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterLayout, 0u, 1u, &Batch.SetHandle, 0u, nullptr);
        vkCmdDraw(Cmd, Batch.VertexCount, 1u, Batch.FirstVertex, 0u);
    }

    vkCmdEndRenderPass(Cmd);

    if (Stats.LiveWindowSwapchain)
    {
        vkEndCommandBuffer(Cmd);
        const VkPipelineStageFlags WaitMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo               Sub {};
        Sub.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Sub.waitSemaphoreCount   = 1u;
        Sub.pWaitSemaphores      = &ImageAcquireSemaphore;
        Sub.pWaitDstStageMask    = &WaitMask;
        Sub.commandBufferCount   = 1u;
        Sub.pCommandBuffers      = &Cmd;
        Sub.signalSemaphoreCount = 1u;
        Sub.pSignalSemaphores    = &RenderDoneSemaphore;

        if (vkQueueSubmit(BoundDevice->AccessQueue(), 1u, &Sub, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            return false;
        }

        VkPresentInfoKHR Pres {};
        Pres.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        Pres.waitSemaphoreCount = 1u;
        Pres.pWaitSemaphores    = &RenderDoneSemaphore;
        Pres.swapchainCount     = 1u;
        Pres.pSwapchains        = &SwapchainHandle;
        Pres.pImageIndices      = &ImageIndex;

        const VkResult PresRes = vkQueuePresentKHR(BoundDevice->AccessQueue(), &Pres);
        vkQueueWaitIdle(BoundDevice->AccessQueue());
        vkFreeCommandBuffers(BoundDevice->AccessDevice(), BoundDevice->AccessCommandAllocator(), 1u, &Cmd);

        if (PresRes == VK_ERROR_OUT_OF_DATE_KHR || PresRes == VK_SUBOPTIMAL_KHR)
        {
            (void)RebuildSwapchainOrOffscreenTarget(TargetWidthPx, TargetHeightPx);
        }
    }
    else
    {
        const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
        (void)BoundDevice->AwaitTimelineTick(SyncPoint);
    }

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
        BoundDevice = nullptr;
        return;
    }

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
