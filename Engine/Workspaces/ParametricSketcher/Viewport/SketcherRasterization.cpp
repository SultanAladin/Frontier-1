/*====================================================================================================================================
                                                      SKETCHERRASTERIZATION.CPP
====================================================================================================================================*/
// 🧩 Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#define STBIRDEF static
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace Frontier::Workspaces::Sketcher
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr VkFormat ColourFormat   = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat IdentityFormat = VK_FORMAT_R32_UINT;
constexpr VkFormat DepthFormat    = VK_FORMAT_D32_SFLOAT;

// 📝 X-ray occluded strokes count as hidden only when they lie more than this many pixels behind the face in front.
constexpr double OccludedMarginPixels = 8.0;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void Require(VkResult Result, const char* Label)
{
    if (Result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(Label) + " failed (VkResult " + std::to_string(static_cast<int>(Result)) + ")");
    }
}

[[nodiscard]] VkShaderModule LoadShaderOrThrow(
    const GeometricRaster::VulkanInterchange& Device,
    const std::filesystem::path&              Dir,
    const char*                               FileName)
{
    const std::filesystem::path FullPath = Dir / FileName;
    const VkShaderModule Module = Device.LoadShaderBinary(FullPath);
    if (Module == VK_NULL_HANDLE)
    {
        throw std::runtime_error("LoadShaderBinary failed for " + FullPath.string());
    }
    return Module;
}

void TransitionImageLayout(
    VkCommandBuffer    Commands,
    VkImage            Image,
    VkImageAspectFlags Aspect,
    VkImageLayout      OldLayout,
    VkImageLayout      NewLayout,
    uint32_t           MipLevels   = 1u,
    uint32_t           LayerCount  = 1u)
{
    VkImageMemoryBarrier Barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    Barrier.oldLayout                       = OldLayout;
    Barrier.newLayout                       = NewLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Image;
    Barrier.subresourceRange.aspectMask     = Aspect;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = MipLevels;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = LayerCount;

    VkPipelineStageFlags SrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags DstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    Barrier.srcAccessMask = 0u;
    Barrier.dstAccessMask = 0u;

    if (OldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        SrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        SrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (OldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        SrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }

    if (NewLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        DstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        DstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (NewLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        DstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (NewLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        DstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        Barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    vkCmdPipelineBarrier(Commands, SrcStage, DstStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
}

struct PipelineRecipe
{
    VkShaderModule                                 Vertex        = VK_NULL_HANDLE;
    VkShaderModule                                 Fragment      = VK_NULL_HANDLE;
    const char*                                    VertexEntry   = "main";
    const char*                                    FragmentEntry = "main";
    std::vector<VkVertexInputBindingDescription>   Bindings      = {};
    std::vector<VkVertexInputAttributeDescription> Attributes    = {};
    bool                                           DepthWrite    = true;
    bool                                           ColourBlend   = false;
    bool                                           IdentityWrite = true;
    bool                                           ColourWrite   = true;
    bool                                           DepthBias     = false;
    VkCompareOp                                    DepthCompare  = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkSampleCountFlagBits                          Samples       = VK_SAMPLE_COUNT_1_BIT;
};

VkPipeline CreatePipeline(VkDevice Device, VkRenderPass RenderPass, VkPipelineLayout Layout, const PipelineRecipe& Recipe)
{
    VkPipelineShaderStageCreateInfo Programs[2]{};
    Programs[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, Recipe.Vertex, Recipe.VertexEntry, nullptr };
    Programs[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, Recipe.Fragment, Recipe.FragmentEntry, nullptr };

    VkPipelineVertexInputStateCreateInfo Input { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    Input.vertexBindingDescriptionCount   = static_cast<uint32_t>(Recipe.Bindings.size());
    Input.pVertexBindingDescriptions      = Recipe.Bindings.data();
    Input.vertexAttributeDescriptionCount = static_cast<uint32_t>(Recipe.Attributes.size());
    Input.pVertexAttributeDescriptions    = Recipe.Attributes.data();

    VkPipelineInputAssemblyStateCreateInfo Assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo Viewport { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1u;
    Viewport.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo Raster { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.cullMode    = VK_CULL_MODE_NONE;
    Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.lineWidth   = 1.0f;
    if (Recipe.DepthBias)
    {
        Raster.depthBiasEnable         = VK_TRUE;
        Raster.depthBiasConstantFactor = -2.0f;
        Raster.depthBiasSlopeFactor    = -1.0f;
    }

    VkPipelineMultisampleStateCreateInfo Samples { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Samples.rasterizationSamples = Recipe.Samples;

    VkPipelineDepthStencilStateCreateInfo DepthState { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthState.depthTestEnable  = VK_TRUE;
    DepthState.depthWriteEnable = Recipe.DepthWrite ? VK_TRUE : VK_FALSE;
    DepthState.depthCompareOp   = Recipe.DepthCompare;

    VkPipelineColorBlendAttachmentState Blends[2]{};
    Blends[0].colorWriteMask = Recipe.ColourWrite
        ? VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
        : VkColorComponentFlags(0);
    if (Recipe.ColourBlend)
    {
        Blends[0].blendEnable         = VK_TRUE;
        Blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        Blends[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        Blends[0].colorBlendOp        = VK_BLEND_OP_ADD;
        Blends[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        Blends[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        Blends[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    Blends[1].colorWriteMask = Recipe.IdentityWrite ? VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT) : VkColorComponentFlags(0);

    VkPipelineColorBlendStateCreateInfo Blend { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    Blend.attachmentCount = 2u;
    Blend.pAttachments    = Blends;

    const VkDynamicState Dynamic[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    DynamicState.dynamicStateCount = 2u;
    DynamicState.pDynamicStates    = Dynamic;

    VkGraphicsPipelineCreateInfo Request { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    Request.stageCount          = 2u;
    Request.pStages             = Programs;
    Request.pVertexInputState   = &Input;
    Request.pInputAssemblyState = &Assembly;
    Request.pViewportState      = &Viewport;
    Request.pRasterizationState = &Raster;
    Request.pMultisampleState   = &Samples;
    Request.pDepthStencilState  = &DepthState;
    Request.pColorBlendState    = &Blend;
    Request.pDynamicState       = &DynamicState;
    Request.layout              = Layout;
    Request.renderPass          = RenderPass;
    Request.subpass             = 0u;

    VkPipeline Pipeline = VK_NULL_HANDLE;
    Require(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &Request, nullptr, &Pipeline), "vkCreateGraphicsPipelines");
    return Pipeline;
}

void Narrow(const CameraMatrix& Source, float Target[16])
{
    for (int Index = 0; Index < 16; ++Index)
    {
        Target[Index] = static_cast<float>(Source[Index]);
    }
}

void Narrow(const std::array<double, 3>& Source, float Target[4], float Fourth)
{
    for (int Index = 0; Index < 3; ++Index)
    {
        Target[Index] = static_cast<float>(Source[Index]);
    }
    Target[3] = Fourth;
}

} // namespace

bool FacesSketcherImagePlane(const SketcherImagePlaneDraw& Draw, const OrbitCameraFrame& Frame) noexcept
{
    if (!Draw.FrontOnlyCondition)
    {
        return true;
    }
    const auto& R = Draw.Right;
    const auto& D = Draw.Down;
    const std::array<double, 3> N { D[1] * R[2] - D[2] * R[1], D[2] * R[0] - D[0] * R[2], D[0] * R[1] - D[1] * R[0] };
    const double Facing = N[0] * Frame.Forward[0] + N[1] * Frame.Forward[1] + N[2] * Frame.Forward[2];
    return Facing < -1.0e-6 * std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

SketcherRasterization::~SketcherRasterization()
{
    Destroy();
}

SketcherRasterization::GpuImage SketcherRasterization::CreateGpuImage(
    uint32_t           Width,
    uint32_t           Height,
    VkFormat           Format,
    VkImageUsageFlags  Usage,
    VkImageAspectFlags Aspect,
    uint32_t           ArrayLayers,
    uint32_t           MipLevels) const
{
    const VkDevice LogicalDevice = Device->AccessDevice();
    const uint32_t EffectiveLayers = std::max<uint32_t>(ArrayLayers, 1u);

    VkImageCreateInfo ImageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInfo.imageType     = VK_IMAGE_TYPE_2D;
    ImageInfo.format        = Format;
    ImageInfo.extent        = { Width, Height, 1u };
    ImageInfo.mipLevels     = MipLevels;
    ImageInfo.arrayLayers   = EffectiveLayers;
    ImageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInfo.usage         = Usage;
    ImageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    GpuImage Out {};
    Out.ImageFormat = Format;
    Out.WidthPx     = Width;
    Out.HeightPx    = Height;
    Require(vkCreateImage(LogicalDevice, &ImageInfo, nullptr, &Out.ImageHandle), "vkCreateImage");

    VkMemoryRequirements Reqs {};
    vkGetImageMemoryRequirements(LogicalDevice, Out.ImageHandle, &Reqs);

    VkPhysicalDeviceMemoryProperties MemProps {};
    vkGetPhysicalDeviceMemoryProperties(Device->AccessPhysicalDevice(), &MemProps);
    uint32_t MemoryTypeIndex = UINT32_MAX;
    for (uint32_t Idx = 0u; Idx < MemProps.memoryTypeCount; ++Idx)
    {
        if ((Reqs.memoryTypeBits & (1u << Idx)) != 0u &&
            (MemProps.memoryTypes[Idx].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        {
            MemoryTypeIndex = Idx;
            break;
        }
    }
    if (MemoryTypeIndex == UINT32_MAX)
    {
        vkDestroyImage(LogicalDevice, Out.ImageHandle, nullptr);
        throw std::runtime_error("No device-local memory type for Sketcher image");
    }

    VkMemoryAllocateInfo AllocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocInfo.allocationSize  = Reqs.size;
    AllocInfo.memoryTypeIndex = MemoryTypeIndex;
    Require(vkAllocateMemory(LogicalDevice, &AllocInfo, nullptr, &Out.MemoryHandle), "vkAllocateMemory (image)");
    Require(vkBindImageMemory(LogicalDevice, Out.ImageHandle, Out.MemoryHandle, 0ull), "vkBindImageMemory");

    VkImageViewCreateInfo ViewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInfo.image                           = Out.ImageHandle;
    ViewInfo.viewType                        = ArrayLayers > 0u ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format                          = Format;
    ViewInfo.subresourceRange.aspectMask     = Aspect;
    ViewInfo.subresourceRange.baseMipLevel   = 0u;
    ViewInfo.subresourceRange.levelCount     = MipLevels;
    ViewInfo.subresourceRange.baseArrayLayer = 0u;
    ViewInfo.subresourceRange.layerCount     = EffectiveLayers;
    Require(vkCreateImageView(LogicalDevice, &ViewInfo, nullptr, &Out.ViewHandle), "vkCreateImageView");
    return Out;
}

void SketcherRasterization::DestroyGpuImage(GpuImage& Image) const noexcept
{
    if (!Device || !Device->Ready())
    {
        Image = {};
        return;
    }
    const VkDevice LogicalDevice = Device->AccessDevice();
    if (Image.ViewHandle != VK_NULL_HANDLE)
    {
        vkDestroyImageView(LogicalDevice, Image.ViewHandle, nullptr);
    }
    if (Image.ImageHandle != VK_NULL_HANDLE)
    {
        vkDestroyImage(LogicalDevice, Image.ImageHandle, nullptr);
    }
    if (Image.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(LogicalDevice, Image.MemoryHandle, nullptr);
    }
    Image = {};
}

GeometricRaster::DeviceBufferResource SketcherRasterization::CreateHostBuffer(VkDeviceSize Size, VkBufferUsageFlags Usage) const
{
    return Device->AllocateBuffer(
        std::max<VkDeviceSize>(Size, 4ull),
        Usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void SketcherRasterization::DestroyBuffer(GeometricRaster::DeviceBufferResource& Buffer) const noexcept
{
    if (Device && Device->Ready())
    {
        Device->ReleaseBuffer(Buffer);
    }
    else
    {
        Buffer = {};
    }
}

void SketcherRasterization::SubmitImmediately(const std::function<void(VkCommandBuffer)>& Record) const
{
    VkCommandBuffer Cmd = Device->BeginSingleTimeCommands();
    Record(Cmd);
    vkEndCommandBuffer(Cmd);

    VkSubmitInfo Submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    Require(vkQueueSubmit(Device->AccessQueue(), 1u, &Submit, VK_NULL_HANDLE), "vkQueueSubmit");
    Require(vkQueueWaitIdle(Device->AccessQueue()), "vkQueueWaitIdle");
    Device->FreeCommandBuffer(Cmd);
}

bool SketcherRasterization::Create(
    const GeometricRaster::VulkanInterchange& Borrowed,
    const std::filesystem::path&              CompiledShaderDirectory,
    std::string&                              Refusal,
    uint32_t                                  /*SampleWish*/)
{
    if (!Borrowed.Ready())
    {
        Refusal = "VulkanInterchange is not initialized";
        return false;
    }
    Device  = &Borrowed;
    Samples = VK_SAMPLE_COUNT_1_BIT;
    const VkDevice LogicalDevice = Device->AccessDevice();

    try
    {
        // ① descriptor set: face states (0), edge states (1), section plane (2)
        VkDescriptorSetLayoutBinding Bindings[3]{};
        Bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        Bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        Bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo LayoutRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        LayoutRequest.bindingCount = 3u;
        LayoutRequest.pBindings    = Bindings;
        Require(vkCreateDescriptorSetLayout(LogicalDevice, &LayoutRequest, nullptr, &BufferLayout), "vkCreateDescriptorSetLayout");

        const VkDescriptorPoolSize PoolSize { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3u };
        VkDescriptorPoolCreateInfo PoolRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PoolRequest.maxSets       = 1u;
        PoolRequest.poolSizeCount = 1u;
        PoolRequest.pPoolSizes    = &PoolSize;
        Require(vkCreateDescriptorPool(LogicalDevice, &PoolRequest, nullptr, &DescriptorPool), "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo DescriptorRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        DescriptorRequest.descriptorPool     = DescriptorPool;
        DescriptorRequest.descriptorSetCount = 1u;
        DescriptorRequest.pSetLayouts        = &BufferLayout;
        Require(vkAllocateDescriptorSets(LogicalDevice, &DescriptorRequest, &BufferSet), "vkAllocateDescriptorSets");

        SectionBuffer = CreateHostBuffer(8u * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memset(SectionBuffer.HostAddress, 0, SectionBuffer.ByteCapacity);
        const VkDescriptorBufferInfo SectionTarget { SectionBuffer.BufferHandle, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet SectionWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        SectionWrite.dstSet          = BufferSet;
        SectionWrite.dstBinding      = 2u;
        SectionWrite.descriptorCount = 1u;
        SectionWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SectionWrite.pBufferInfo     = &SectionTarget;
        vkUpdateDescriptorSets(LogicalDevice, 1u, &SectionWrite, 0u, nullptr);

        // ② pipeline layout: set 0 + 128-byte push constants
        const VkPushConstantRange Push { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, 128u };
        VkPipelineLayoutCreateInfo PipelineLayoutRequest { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PipelineLayoutRequest.setLayoutCount         = 1u;
        PipelineLayoutRequest.pSetLayouts            = &BufferLayout;
        PipelineLayoutRequest.pushConstantRangeCount = 1u;
        PipelineLayoutRequest.pPushConstantRanges    = &Push;
        Require(vkCreatePipelineLayout(LogicalDevice, &PipelineLayoutRequest, nullptr, &PipelineLayout), "vkCreatePipelineLayout");

        // ③ image planes: set 1 = combined image sampler
        constexpr uint32_t PictureLimit = 16u;
        const VkDescriptorSetLayoutBinding PictureBinding { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo PictureLayoutRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        PictureLayoutRequest.bindingCount = 1u;
        PictureLayoutRequest.pBindings    = &PictureBinding;
        Require(vkCreateDescriptorSetLayout(LogicalDevice, &PictureLayoutRequest, nullptr, &PictureLayout), "vkCreateDescriptorSetLayout (pictures)");

        const VkDescriptorPoolSize PictureSize { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PictureLimit };
        VkDescriptorPoolCreateInfo PicturePoolRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PicturePoolRequest.maxSets       = PictureLimit;
        PicturePoolRequest.poolSizeCount = 1u;
        PicturePoolRequest.pPoolSizes    = &PictureSize;
        Require(vkCreateDescriptorPool(LogicalDevice, &PicturePoolRequest, nullptr, &PicturePool), "vkCreateDescriptorPool (pictures)");

        VkSamplerCreateInfo SamplerRequest { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        SamplerRequest.magFilter    = VK_FILTER_LINEAR;
        SamplerRequest.minFilter    = VK_FILTER_LINEAR;
        SamplerRequest.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        SamplerRequest.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.maxLod       = VK_LOD_CLAMP_NONE;
        Require(vkCreateSampler(LogicalDevice, &SamplerRequest, nullptr, &PictureSampler), "vkCreateSampler");

        const VkDescriptorSetLayout PlaneSets[2] = { BufferLayout, PictureLayout };
        VkPipelineLayoutCreateInfo PlaneLayoutRequest { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PlaneLayoutRequest.setLayoutCount         = 2u;
        PlaneLayoutRequest.pSetLayouts            = PlaneSets;
        PlaneLayoutRequest.pushConstantRangeCount = 1u;
        PlaneLayoutRequest.pPushConstantRanges    = &Push;
        Require(vkCreatePipelineLayout(LogicalDevice, &PlaneLayoutRequest, nullptr, &PlaneLayout), "vkCreatePipelineLayout (pictures)");

        // ④ matcaps: set 1 in own pool
        const VkDescriptorPoolSize MatcapSize { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u };
        VkDescriptorPoolCreateInfo MatcapPoolRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        MatcapPoolRequest.maxSets       = 1u;
        MatcapPoolRequest.poolSizeCount = 1u;
        MatcapPoolRequest.pPoolSizes    = &MatcapSize;
        Require(vkCreateDescriptorPool(LogicalDevice, &MatcapPoolRequest, nullptr, &MatcapPool), "vkCreateDescriptorPool (matcaps)");

        VkDescriptorSetAllocateInfo MatcapRequest { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        MatcapRequest.descriptorPool     = MatcapPool;
        MatcapRequest.descriptorSetCount = 1u;
        MatcapRequest.pSetLayouts        = &PictureLayout;
        Require(vkAllocateDescriptorSets(LogicalDevice, &MatcapRequest, &MatcapSet), "vkAllocateDescriptorSets (matcaps)");

        CreateRenderPass();
        CreatePipelines(CompiledShaderDirectory);
        UploadMatcaps({});
        ProbeReadback = CreateHostBuffer(16u, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        AssignStates({}, {});
        CreateTargets(64u, 64u);
        return true;
    }
    catch (const std::exception& Failure)
    {
        Refusal = Failure.what();
        Destroy();
        return false;
    }
}

void SketcherRasterization::CreateRenderPass()
{
    VkAttachmentDescription Attachments[3]{};
    // 0: Colour (RGBA8)
    Attachments[0].format         = ColourFormat;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 1: Identity (R32_UINT)
    Attachments[1].format         = IdentityFormat;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 2: Depth (D32_SFLOAT)
    Attachments[2].format         = DepthFormat;
    Attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentReference ColourRefs[2] = {
        { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };
    const VkAttachmentReference DepthRef { 2u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Subpass {};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = 2u;
    Subpass.pColorAttachments       = ColourRefs;
    Subpass.pDepthStencilAttachment = &DepthRef;

    VkRenderPassCreateInfo RpInfo { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    RpInfo.attachmentCount = 3u;
    RpInfo.pAttachments    = Attachments;
    RpInfo.subpassCount    = 1u;
    RpInfo.pSubpasses      = &Subpass;
    Require(vkCreateRenderPass(Device->AccessDevice(), &RpInfo, nullptr, &RenderPass), "vkCreateRenderPass");
}

void SketcherRasterization::CreatePipelines(const std::filesystem::path& CompiledShaderDirectory)
{
    const VkDevice LogicalDevice = Device->AccessDevice();

    const VkShaderModule SurfaceVertex   = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherSurface.vert.spv");
    const VkShaderModule SurfaceFragment = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherSurface.frag.spv");
    const VkShaderModule EdgeVertex      = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherEdge.vert.spv");
    const VkShaderModule EdgeFragment    = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherEdge.frag.spv");
    const VkShaderModule EdgeOccluded    = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherEdgeOccluded.frag.spv");
    const VkShaderModule EdgeIdentity    = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherEdgeIdentity.frag.spv");
    const VkShaderModule GridVertex      = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherGrid.vert.spv");
    const VkShaderModule GridFragment    = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherGrid.frag.spv");
    const VkShaderModule PlaneVertex     = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherImagePlane.vert.spv");
    const VkShaderModule PlaneFragment   = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherImagePlane.frag.spv");
    const VkShaderModule GizmoVertex     = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherGizmo.vert.spv");
    const VkShaderModule GizmoFragment   = LoadShaderOrThrow(*Device, CompiledShaderDirectory, "SketcherGizmo.frag.spv");

    PipelineRecipe Surface;
    Surface.Samples    = Samples;
    Surface.Vertex     = SurfaceVertex;
    Surface.Fragment   = SurfaceFragment;
    Surface.Bindings   = { { 0u, sizeof(SketcherSceneCorner), VK_VERTEX_INPUT_RATE_VERTEX } };
    Surface.Attributes = { { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 0u }, { 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 12u }, { 2u, 0u, VK_FORMAT_R32_UINT, 24u } };
    SurfacePipeline    = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, Surface);

    PipelineRecipe SurfaceDepth = Surface;
    SurfaceDepth.ColourWrite    = false;
    SurfaceDepthPipeline        = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, SurfaceDepth);

    PipelineRecipe SurfaceGhost = Surface;
    SurfaceGhost.DepthWrite     = false;
    SurfaceGhost.ColourBlend    = true;
    SurfaceGhost.IdentityWrite  = false;
    SurfaceGhostPipeline        = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, SurfaceGhost);

    PipelineRecipe CutterDepth  = SurfaceDepth;
    CutterDepth.DepthBias       = true;
    CutterDepthPipeline         = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, CutterDepth);

    PipelineRecipe CutterGhost  = SurfaceGhost;
    CutterGhost.DepthBias       = true;
    CutterGhostPipeline         = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, CutterGhost);

    PipelineRecipe Edge;
    Edge.Samples       = Samples;
    Edge.Vertex        = EdgeVertex;
    Edge.Fragment      = EdgeFragment;
    Edge.Bindings      = { { 0u, sizeof(SketcherSceneSegment), VK_VERTEX_INPUT_RATE_INSTANCE } };
    Edge.Attributes    = { { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 0u }, { 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 12u }, { 2u, 0u, VK_FORMAT_R32_UINT, 24u } };
    Edge.ColourBlend   = true;
    Edge.DepthWrite    = false;
    Edge.IdentityWrite = false;
    EdgePipeline       = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, Edge);

    PipelineRecipe EdgeCodes = Edge;
    EdgeCodes.Fragment       = EdgeIdentity;
    EdgeCodes.ColourWrite    = false;
    EdgeCodes.IdentityWrite  = true;
    EdgeIdentityPipeline     = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, EdgeCodes);

    PipelineRecipe Occluded = Edge;
    Occluded.Fragment       = EdgeOccluded;
    Occluded.DepthWrite     = false;
    Occluded.DepthCompare   = VK_COMPARE_OP_GREATER;
    EdgeOccludedPipeline    = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, Occluded);

    PipelineRecipe OccludedCodes = EdgeCodes;
    OccludedCodes.DepthCompare   = VK_COMPARE_OP_GREATER;
    EdgeOccludedIdentityPipeline = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, OccludedCodes);

    PipelineRecipe Grid;
    Grid.Samples       = Samples;
    Grid.Vertex        = GridVertex;
    Grid.Fragment      = GridFragment;
    Grid.ColourBlend   = true;
    Grid.DepthWrite    = false;
    Grid.IdentityWrite = false;
    GridPipeline       = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, Grid);

    PipelineRecipe Plane;
    Plane.Samples       = Samples;
    Plane.Vertex        = PlaneVertex;
    Plane.Fragment      = PlaneFragment;
    Plane.ColourBlend   = true;
    Plane.DepthWrite    = false;
    Plane.IdentityWrite = false;
    PlanePipeline       = CreatePipeline(LogicalDevice, RenderPass, PlaneLayout, Plane);

    PipelineRecipe Gizmo;
    Gizmo.Samples       = Samples;
    Gizmo.Vertex        = GizmoVertex;
    Gizmo.Fragment      = GizmoFragment;
    Gizmo.Bindings      = { { 0u, sizeof(GizmoVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
    Gizmo.Attributes    = {
        { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 0u },
        { 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 12u },
        { 2u, 0u, VK_FORMAT_R8G8B8A8_UNORM,   24u },
        { 3u, 0u, VK_FORMAT_R32_UINT,         28u },
    };
    Gizmo.ColourBlend   = true;
    Gizmo.IdentityWrite = false;
    GizmoPipeline       = CreatePipeline(LogicalDevice, RenderPass, PipelineLayout, Gizmo);

    for (VkShaderModule Module : { SurfaceVertex, SurfaceFragment, EdgeVertex, EdgeFragment, EdgeOccluded, EdgeIdentity,
                                   GridVertex, GridFragment, PlaneVertex, PlaneFragment, GizmoVertex, GizmoFragment })
    {
        vkDestroyShaderModule(LogicalDevice, Module, nullptr);
    }
}

void SketcherRasterization::Destroy()
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    const VkDevice LogicalDevice = Device->AccessDevice();
    vkDeviceWaitIdle(LogicalDevice);
    DestroyTargets();
    for (GeometricRaster::DeviceBufferResource* Buffer : { &ProbeReadback, &CornerBuffer, &SegmentBuffer, &FaceStateBuffer, &EdgeStateBuffer, &SectionBuffer, &GizmoBuffer })
    {
        DestroyBuffer(*Buffer);
    }
    ReleasePictures();
    DestroyGpuImage(MatcapArray);
    if (MatcapPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(LogicalDevice, MatcapPool, nullptr);
    MatcapPool   = VK_NULL_HANDLE;
    MatcapSet    = VK_NULL_HANDLE;
    MatcapLayers = 0u;

    for (VkPipeline* Pipeline : { &SurfacePipeline, &SurfaceDepthPipeline, &SurfaceGhostPipeline, &CutterDepthPipeline, &CutterGhostPipeline,
                                  &EdgePipeline, &EdgeIdentityPipeline, &EdgeOccludedPipeline, &EdgeOccludedIdentityPipeline,
                                  &GridPipeline, &PlanePipeline, &GizmoPipeline })
    {
        if (*Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(LogicalDevice, *Pipeline, nullptr);
        }
        *Pipeline = VK_NULL_HANDLE;
    }
    if (RenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(LogicalDevice, RenderPass, nullptr);
    if (PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(LogicalDevice, PipelineLayout, nullptr);
    if (DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(LogicalDevice, DescriptorPool, nullptr);
    if (BufferLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(LogicalDevice, BufferLayout, nullptr);
    if (PlaneLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(LogicalDevice, PlaneLayout, nullptr);
    if (PicturePool != VK_NULL_HANDLE) vkDestroyDescriptorPool(LogicalDevice, PicturePool, nullptr);
    if (PictureLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(LogicalDevice, PictureLayout, nullptr);
    if (PictureSampler != VK_NULL_HANDLE) vkDestroySampler(LogicalDevice, PictureSampler, nullptr);

    RenderPass         = VK_NULL_HANDLE;
    PlaneLayout        = VK_NULL_HANDLE;
    PicturePool        = VK_NULL_HANDLE;
    PictureLayout      = VK_NULL_HANDLE;
    PictureSampler     = VK_NULL_HANDLE;
    PipelineLayout     = VK_NULL_HANDLE;
    DescriptorPool     = VK_NULL_HANDLE;
    BufferLayout       = VK_NULL_HANDLE;
    BufferSet          = VK_NULL_HANDLE;
    UploadedGeneration = UINT64_MAX;
    Device             = nullptr;
}

void SketcherRasterization::CreateTargets(uint32_t Width, uint32_t Height)
{
    Colour   = CreateGpuImage(Width, Height, ColourFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Identity = CreateGpuImage(Width, Height, IdentityFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Depth    = CreateGpuImage(Width, Height, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    const VkImageView Views[3] = { Colour.ViewHandle, Identity.ViewHandle, Depth.ViewHandle };
    VkFramebufferCreateInfo FbInfo { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    FbInfo.renderPass      = RenderPass;
    FbInfo.attachmentCount = 3u;
    FbInfo.pAttachments    = Views;
    FbInfo.width           = Width;
    FbInfo.height          = Height;
    FbInfo.layers          = 1u;
    Require(vkCreateFramebuffer(Device->AccessDevice(), &FbInfo, nullptr, &Framebuffer), "vkCreateFramebuffer");

    ColourReadback   = CreateHostBuffer(VkDeviceSize(Width) * Height * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    IdentityReadback = CreateHostBuffer(VkDeviceSize(Width) * Height * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    ++BuiltTargets;

    SubmitImmediately([&](VkCommandBuffer Commands)
    {
        TransitionImageLayout(Commands, Colour.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void SketcherRasterization::DestroyTargets()
{
    if (Device && Device->Ready() && Framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(Device->AccessDevice(), Framebuffer, nullptr);
    }
    Framebuffer = VK_NULL_HANDLE;
    for (GpuImage* Image : { &Colour, &Identity, &Depth })
    {
        if (Image->ImageHandle != VK_NULL_HANDLE)
        {
            DestroyGpuImage(*Image);
        }
    }
    for (GeometricRaster::DeviceBufferResource* Buffer : { &ColourReadback, &IdentityReadback })
    {
        DestroyBuffer(*Buffer);
    }
}

void SketcherRasterization::Resize(uint32_t Width, uint32_t Height)
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    Width  = std::max<uint32_t>(Width, 8u);
    Height = std::max<uint32_t>(Height, 8u);
    if (Width == Colour.WidthPx && Height == Colour.HeightPx)
    {
        return;
    }
    try
    {
        vkDeviceWaitIdle(Device->AccessDevice());
        DestroyTargets();
        CreateTargets(Width, Height);
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SketcherRasterization] Resize(%u, %u) failed: %s\n", Width, Height, Failure.what());
    }
}

void SketcherRasterization::UploadScene(const SketcherScene& Scene)
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    try
    {
        vkDeviceWaitIdle(Device->AccessDevice());
        DestroyBuffer(CornerBuffer);
        DestroyBuffer(SegmentBuffer);
        CornerCount       = static_cast<uint32_t>(Scene.Corners.size());
        SolidCornerCount  = Scene.SolidCornerCount > 0u && Scene.SolidCornerCount <= CornerCount ? Scene.SolidCornerCount : (Scene.SolidCornerCount == 0u && !Scene.Bodies.empty() ? 0u : CornerCount);
        SegmentCount      = static_cast<uint32_t>(Scene.Segments.size());
        SolidSegmentCount = Scene.SolidSegmentCount > 0u && Scene.SolidSegmentCount <= SegmentCount ? Scene.SolidSegmentCount : (Scene.SolidSegmentCount == 0u && !Scene.Bodies.empty() ? 0u : SegmentCount);
        CornerBuffer      = CreateHostBuffer(sizeof(SketcherSceneCorner) * std::max<size_t>(CornerCount, 1u), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        SegmentBuffer     = CreateHostBuffer(sizeof(SketcherSceneSegment) * std::max<size_t>(SegmentCount, 1u), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (CornerCount > 0u && CornerBuffer.HostAddress != nullptr)
        {
            std::memcpy(CornerBuffer.HostAddress, Scene.Corners.data(), sizeof(SketcherSceneCorner) * CornerCount);
        }
        if (SegmentCount > 0u && SegmentBuffer.HostAddress != nullptr)
        {
            std::memcpy(SegmentBuffer.HostAddress, Scene.Segments.data(), sizeof(SketcherSceneSegment) * SegmentCount);
        }
        UploadedGeneration = Scene.Generation;
        AssignStates(std::vector<uint32_t>(Scene.Faces.size(), 0u), std::vector<uint32_t>(Scene.Edges.size(), 0u));
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SketcherRasterization] UploadScene failed: %s\n", Failure.what());
    }
}

void SketcherRasterization::ReleasePictures()
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    for (PictureTexture& Texture : Pictures)
    {
        DestroyGpuImage(Texture.Image);
    }
    Pictures.clear();
    if (PicturePool != VK_NULL_HANDLE)
    {
        vkResetDescriptorPool(Device->AccessDevice(), PicturePool, 0u);
    }
}

void SketcherRasterization::UploadPictures(const std::vector<SketcherPicture>& Sources)
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    try
    {
        vkDeviceWaitIdle(Device->AccessDevice());
        ReleasePictures();
        for (const SketcherPicture& Source : Sources)
        {
            if (Pictures.size() >= 16u)
            {
                break;
            }
            const bool ReadyCondition = Source.Refusal.empty() && Source.Width > 0 && Source.Height > 0 &&
                Source.Pixels.size() == size_t(Source.Width) * Source.Height * 4u;
            const uint32_t Width  = ReadyCondition ? Source.Width : 1u;
            const uint32_t Height = ReadyCondition ? Source.Height : 1u;
            static const uint8_t Placeholder[4] = { 255, 255, 255, 255 };

            std::vector<VkBufferImageCopy> Copies;
            VkDeviceSize Bytes = 0;
            for (uint32_t W = Width, H = Height, Level = 0u; ; ++Level)
            {
                VkBufferImageCopy Copy {};
                Copy.bufferOffset     = Bytes;
                Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, Level, 0u, 1u };
                Copy.imageExtent      = { W, H, 1u };
                Copies.push_back(Copy);
                Bytes += VkDeviceSize(W) * H * 4u;
                if (W == 1u && H == 1u)
                {
                    break;
                }
                W = std::max(1u, W / 2u);
                H = std::max(1u, H / 2u);
            }
            const uint32_t MipLevels = static_cast<uint32_t>(Copies.size());

            PictureTexture Texture;
            Texture.Image = CreateGpuImage(
                Width,
                Height,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                0u,
                MipLevels);
            GeometricRaster::DeviceBufferResource Staging = CreateHostBuffer(Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            auto* Pixels = static_cast<unsigned char*>(Staging.HostAddress);
            std::memcpy(Pixels, ReadyCondition ? Source.Pixels.data() : Placeholder, size_t(Width) * Height * 4u);
            for (size_t Level = 1u; Level < Copies.size(); ++Level)
            {
                const auto& A = Copies[Level - 1u];
                const auto& B = Copies[Level];
                if (!stbir_resize_uint8_linear(
                        Pixels + A.bufferOffset, int(A.imageExtent.width), int(A.imageExtent.height), 0,
                        Pixels + B.bufferOffset, int(B.imageExtent.width), int(B.imageExtent.height), 0, STBIR_RGBA))
                {
                    throw std::runtime_error("reference image mip filtering failed");
                }
            }
            SubmitImmediately([&](VkCommandBuffer Commands)
            {
                TransitionImageLayout(Commands, Texture.Image.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, MipLevels);
                vkCmdCopyBufferToImage(Commands, Staging.BufferHandle, Texture.Image.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, MipLevels, Copies.data());
                TransitionImageLayout(Commands, Texture.Image.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, MipLevels);
            });
            DestroyBuffer(Staging);

            VkDescriptorSetAllocateInfo Request { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            Request.descriptorPool     = PicturePool;
            Request.descriptorSetCount = 1u;
            Request.pSetLayouts        = &PictureLayout;
            Require(vkAllocateDescriptorSets(Device->AccessDevice(), &Request, &Texture.Set), "vkAllocateDescriptorSets (picture)");

            const VkDescriptorImageInfo Target { PictureSampler, Texture.Image.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet Write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            Write.dstSet          = Texture.Set;
            Write.dstBinding      = 0u;
            Write.descriptorCount = 1u;
            Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            Write.pImageInfo      = &Target;
            vkUpdateDescriptorSets(Device->AccessDevice(), 1u, &Write, 0u, nullptr);
            Pictures.push_back(Texture);
        }
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SketcherRasterization] UploadPictures failed: %s\n", Failure.what());
    }
}

void SketcherRasterization::UploadMatcaps(const std::vector<SketcherPicture>& Layers)
{
    if (!Device || !Device->Ready())
    {
        return;
    }
    try
    {
        vkDeviceWaitIdle(Device->AccessDevice());
        DestroyGpuImage(MatcapArray);

        const uint32_t Extent     = Layers.empty() || Layers.front().Width == 0 ? 1u : Layers.front().Width;
        const uint32_t Count      = std::max<uint32_t>(static_cast<uint32_t>(Layers.size()), 1u);
        const size_t   LayerBytes = size_t(Extent) * Extent * 4u;

        GeometricRaster::DeviceBufferResource Staging = CreateHostBuffer(VkDeviceSize(LayerBytes) * Count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        for (uint32_t Layer = 0u; Layer < Count; ++Layer)
        {
            uint8_t* Target = static_cast<uint8_t*>(Staging.HostAddress) + LayerBytes * Layer;
            const bool FitCondition = Layer < Layers.size() && Layers[Layer].Width == Extent && Layers[Layer].Height == Extent &&
                Layers[Layer].Pixels.size() == LayerBytes;
            if (FitCondition)
            {
                std::memcpy(Target, Layers[Layer].Pixels.data(), LayerBytes);
            }
            else
            {
                std::memset(Target, 128, LayerBytes);
            }
        }

        MatcapArray = CreateGpuImage(
            Extent,
            Extent,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            Count);
        SubmitImmediately([&](VkCommandBuffer Commands)
        {
            TransitionImageLayout(Commands, MatcapArray.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, Count);
            VkBufferImageCopy Copy {};
            Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, Count };
            Copy.imageExtent      = { Extent, Extent, 1u };
            vkCmdCopyBufferToImage(Commands, Staging.BufferHandle, MatcapArray.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
            TransitionImageLayout(Commands, MatcapArray.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u, Count);
        });
        DestroyBuffer(Staging);

        const VkDescriptorImageInfo Target { PictureSampler, MatcapArray.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet Write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        Write.dstSet          = MatcapSet;
        Write.dstBinding      = 0u;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Write.pImageInfo      = &Target;
        vkUpdateDescriptorSets(Device->AccessDevice(), 1u, &Write, 0u, nullptr);
        MatcapLayers = Layers.empty() ? 0u : Count;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SketcherRasterization] UploadMatcaps failed: %s\n", Failure.what());
    }
}

void SketcherRasterization::AssignStates(const std::vector<uint32_t>& FaceStates, const std::vector<uint32_t>& EdgeStates)
{
    const bool ReshapeCondition = FaceStateBuffer.BufferHandle == VK_NULL_HANDLE || FaceStates.size() != FaceCount || EdgeStates.size() != EdgeCount;
    if (ReshapeCondition)
    {
        vkDeviceWaitIdle(Device->AccessDevice());
        DestroyBuffer(FaceStateBuffer);
        DestroyBuffer(EdgeStateBuffer);
        FaceCount       = static_cast<uint32_t>(FaceStates.size());
        EdgeCount       = static_cast<uint32_t>(EdgeStates.size());
        FaceStateBuffer = CreateHostBuffer(4u * std::max<size_t>(FaceCount, 1u), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        EdgeStateBuffer = CreateHostBuffer(4u * std::max<size_t>(EdgeCount, 1u), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDescriptorBufferInfo Targets[2] = { { FaceStateBuffer.BufferHandle, 0, VK_WHOLE_SIZE }, { EdgeStateBuffer.BufferHandle, 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet Writes[2]{};
        for (uint32_t Binding = 0u; Binding < 2u; ++Binding)
        {
            Writes[Binding]                 = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            Writes[Binding].dstSet          = BufferSet;
            Writes[Binding].dstBinding      = Binding;
            Writes[Binding].descriptorCount = 1u;
            Writes[Binding].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[Binding].pBufferInfo     = &Targets[Binding];
        }
        vkUpdateDescriptorSets(Device->AccessDevice(), 2u, Writes, 0u, nullptr);
    }
    std::memset(FaceStateBuffer.HostAddress, 0, FaceStateBuffer.ByteCapacity);
    std::memset(EdgeStateBuffer.HostAddress, 0, EdgeStateBuffer.ByteCapacity);
    if (!FaceStates.empty())
    {
        std::memcpy(FaceStateBuffer.HostAddress, FaceStates.data(), 4u * FaceStates.size());
    }
    if (!EdgeStates.empty())
    {
        std::memcpy(EdgeStateBuffer.HostAddress, EdgeStates.data(), 4u * EdgeStates.size());
    }
}

void SketcherRasterization::Record(
    VkCommandBuffer              Commands,
    const OrbitCameraFrame&      Frame,
    const SketcherViewportStyle& Style,
    bool                         ReadbackCondition,
    int                          ProbeX,
    int                          ProbeY)
{
    SketcherRasterPane Whole;
    Whole.OffsetX = 0;
    Whole.OffsetY = 0;
    Whole.Width   = Colour.WidthPx;
    Whole.Height  = Colour.HeightPx;
    Whole.Frame   = Frame;
    Whole.Style   = Style;
    RecordPanes(Commands, { std::move(Whole) }, Style.Background, ReadbackCondition, ProbeX, ProbeY);
}

void SketcherRasterization::RecordPanes(
    VkCommandBuffer                        Commands,
    const std::vector<SketcherRasterPane>& Panes,
    const float                            Background[4],
    bool                                   ReadbackCondition,
    int                                    ProbeX,
    int                                    ProbeY)
{
    std::vector<uint32_t> GizmoFirst(Panes.size(), 0u);
    std::vector<uint32_t> GizmoCount(Panes.size(), 0u);
    size_t TotalGizmoVerts = 0u;
    for (size_t Idx = 0u; Idx < Panes.size(); ++Idx)
    {
        GizmoFirst[Idx] = static_cast<uint32_t>(TotalGizmoVerts);
        GizmoCount[Idx] = static_cast<uint32_t>(Panes[Idx].Style.Gizmo.Vertices.size());
        TotalGizmoVerts += Panes[Idx].Style.Gizmo.Vertices.size();
    }
    if (TotalGizmoVerts > 0u && GizmoPipeline != VK_NULL_HANDLE)
    {
        const VkDeviceSize Needed = sizeof(GizmoVertex) * TotalGizmoVerts;
        if (GizmoBuffer.ByteCapacity < Needed)
        {
            vkDeviceWaitIdle(Device->AccessDevice());
            DestroyBuffer(GizmoBuffer);
            GizmoBuffer = CreateHostBuffer(std::max<VkDeviceSize>(Needed * 2u, 64u * 1024u), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        }
        auto* Dst = static_cast<GizmoVertex*>(GizmoBuffer.HostAddress);
        for (size_t Idx = 0u; Idx < Panes.size(); ++Idx)
        {
            if (GizmoCount[Idx] > 0u)
            {
                std::memcpy(Dst + GizmoFirst[Idx], Panes[Idx].Style.Gizmo.Vertices.data(), sizeof(GizmoVertex) * GizmoCount[Idx]);
            }
        }
    }

    if (!Panes.empty() && SectionBuffer.HostAddress != nullptr)
    {
        const OrbitCameraFrame&      FirstFrame = Panes.front().Frame;
        const SketcherViewportStyle& FirstStyle = Panes.front().Style;
        const float Block[8] = {
            float(FirstStyle.SectionPlane[0]), float(FirstStyle.SectionPlane[1]), float(FirstStyle.SectionPlane[2]), float(FirstStyle.SectionPlane[3]),
            float(FirstFrame.Eye[0]),          float(FirstFrame.Eye[1]),          float(FirstFrame.Eye[2]),          FirstStyle.SectionEnabled ? 1.0f : 0.0f
        };
        std::memcpy(SectionBuffer.HostAddress, Block, sizeof(Block));
    }

    VkClearValue Clears[3]{};
    std::memcpy(Clears[0].color.float32, Background, sizeof(float) * 4);
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = 0u;
    Clears[1].color.uint32[2] = 0u;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil    = { 1.0f, 0u };

    VkRenderPassBeginInfo RpBegin { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    RpBegin.renderPass        = RenderPass;
    RpBegin.framebuffer       = Framebuffer;
    RpBegin.renderArea        = { { 0, 0 }, { Colour.WidthPx, Colour.HeightPx } };
    RpBegin.clearValueCount   = 3u;
    RpBegin.pClearValues      = Clears;
    vkCmdBeginRenderPass(Commands, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

    const VkShaderStageFlags PushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDeviceSize       Origin     = 0ull;

    for (size_t PaneIdx = 0u; PaneIdx < Panes.size(); ++PaneIdx)
    {
        const SketcherRasterPane&    Pane  = Panes[PaneIdx];
        const OrbitCameraFrame&      Frame = Pane.Frame;
        const SketcherViewportStyle& Style = Pane.Style;

        const int32_t  ClampedX = std::clamp<int32_t>(Pane.OffsetX, 0, int32_t(Colour.WidthPx));
        const int32_t  ClampedY = std::clamp<int32_t>(Pane.OffsetY, 0, int32_t(Colour.HeightPx));
        const uint32_t MaxW     = uint32_t(int32_t(Colour.WidthPx) - ClampedX);
        const uint32_t MaxH     = uint32_t(int32_t(Colour.HeightPx) - ClampedY);
        const uint32_t ClampedW = std::min<uint32_t>(Pane.Width, MaxW);
        const uint32_t ClampedH = std::min<uint32_t>(Pane.Height, MaxH);
        if (ClampedW < 2u || ClampedH < 2u)
        {
            continue;
        }

        const VkViewport Viewport { float(ClampedX), float(ClampedY), float(ClampedW), float(ClampedH), 0.0f, 1.0f };
        const VkRect2D   Scissor { { ClampedX, ClampedY }, { ClampedW, ClampedH } };
        vkCmdSetViewport(Commands, 0u, 1u, &Viewport);
        vkCmdSetScissor(Commands, 0u, 1u, &Scissor);
        vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0u, 1u, &BufferSet, 0u, nullptr);

        SketcherProjectionBlock Projection;
        Narrow(Frame.ClipFromWorld, Projection.ClipFromWorld);
        Narrow(Frame.Right, Projection.ViewRight, Frame.OrthographicEnabled ? 1.0f : 0.0f);
        Narrow(Frame.Up, Projection.ViewUp, Style.ZebraStripes);
        Narrow(Frame.Forward, Projection.ViewForward, Style.XRayEnabled ? std::clamp(Style.XRayOpacity, 0.01f, 1.0f) : 0.0f);
        Projection.DisplayStroke[0] = float(ClampedW);
        Projection.DisplayStroke[1] = float(ClampedH);
        Projection.DisplayStroke[2] = Style.EdgeStroke;
        Projection.DisplayStroke[3] = Style.EdgeDepthMargin;

        const auto DrawSurfaceSpan = [&](VkPipeline Pipeline, uint32_t FirstCorner, uint32_t SpanCount, float OpacityOverride)
        {
            if (!Style.SurfacesEnabled || SpanCount == 0u || CornerBuffer.BufferHandle == VK_NULL_HANDLE)
            {
                return;
            }
            SketcherProjectionBlock Block = Projection;
            if (OpacityOverride >= 0.0f)
            {
                Block.ViewForward[3] = OpacityOverride;
            }
            const VkDescriptorSet SurfaceSets[2] = { BufferSet, MatcapSet };
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 0u, 2u, SurfaceSets, 0u, nullptr);
            vkCmdPushConstants(Commands, PlaneLayout, PushStages, 0u, sizeof(Block), &Block);
            vkCmdBindVertexBuffers(Commands, 0u, 1u, &CornerBuffer.BufferHandle, &Origin);
            vkCmdDraw(Commands, SpanCount, 1u, FirstCorner, 0u);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0u, 1u, &BufferSet, 0u, nullptr);
        };
        const auto DrawSurfaces = [&](VkPipeline Pipeline) { DrawSurfaceSpan(Pipeline, 0u, CornerCount, -1.0f); };

        const auto DrawEdgeSpan = [&](VkPipeline Pipeline, VkPipeline CodesPipeline, float DepthMargin, uint32_t FirstSegment, uint32_t SpanSegments)
        {
            if (!Style.EdgesEnabled || SpanSegments == 0u || SegmentBuffer.BufferHandle == VK_NULL_HANDLE)
            {
                return;
            }
            SketcherProjectionBlock Stroke = Projection;
            Stroke.DisplayStroke[3] = DepthMargin;
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0u, sizeof(Stroke), &Stroke);
            vkCmdBindVertexBuffers(Commands, 0u, 1u, &SegmentBuffer.BufferHandle, &Origin);
            for (VkPipeline Each : { Pipeline, CodesPipeline })
            {
                vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Each);
                vkCmdDraw(Commands, 6u, SpanSegments, 0u, FirstSegment);
            }
        };
        const auto DrawEdges = [&](VkPipeline Pipeline, VkPipeline CodesPipeline, float DepthMargin) { DrawEdgeSpan(Pipeline, CodesPipeline, DepthMargin, 0u, SegmentCount); };

        const auto DrawLiveCutters = [&]()
        {
            const uint32_t CutterCorners  = CornerCount > SolidCornerCount ? CornerCount - SolidCornerCount : 0u;
            const uint32_t CutterSegments = SegmentCount > SolidSegmentCount ? SegmentCount - SolidSegmentCount : 0u;
            if (CutterCorners > 0u)
            {
                DrawSurfaceSpan(CutterDepthPipeline, SolidCornerCount, CutterCorners, 0.34f);
                DrawSurfaceSpan(CutterGhostPipeline, SolidCornerCount, CutterCorners, 0.34f);
            }
            if (CutterSegments > 0u)
            {
                DrawEdgeSpan(EdgeOccludedPipeline, EdgeOccludedIdentityPipeline, -float(OccludedMarginPixels * Frame.WorldPerPixel), SolidSegmentCount, CutterSegments);
            }
        };

        const auto DrawPlanes = [&]()
        {
            if (Style.ImagePlanes.empty() || PlanePipeline == VK_NULL_HANDLE)
            {
                return;
            }
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlanePipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 0u, 1u, &BufferSet, 0u, nullptr);
            for (const SketcherImagePlaneDraw& Draw : Style.ImagePlanes)
            {
                if (Draw.Picture >= Pictures.size() || !FacesSketcherImagePlane(Draw, Frame))
                {
                    continue;
                }
                SketcherImagePlaneBlock Block;
                Narrow(Frame.ClipFromWorld, Block.ClipFromWorld);
                Narrow(Draw.Corner, Block.Corner, Draw.Opacity);
                Narrow(Draw.Right, Block.Right, Draw.Mode);
                Narrow(Draw.Down, Block.Down, 0.0f);
                std::memcpy(Block.Crop, Draw.Crop.data(), sizeof(Block.Crop));
                vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 1u, 1u, &Pictures[Draw.Picture].Set, 0u, nullptr);
                vkCmdPushConstants(Commands, PlaneLayout, PushStages, 0u, sizeof(Block), &Block);
                vkCmdDraw(Commands, 6u, 1u, 0u, 0u);
            }
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0u, 1u, &BufferSet, 0u, nullptr);
        };

        const auto DrawGrid = [&]()
        {
            if (!Style.GridEnabled)
            {
                return;
            }
            static constexpr int   PlaneAxes[3][3]   = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 } };
            static constexpr float AxisColours[3][4] = {
                { 0.86f, 0.26f, 0.26f, 1.0f },
                { 0.30f, 0.80f, 0.34f, 1.0f },
                { 0.30f, 0.52f, 0.92f, 1.0f },
            };
            const int Plane = std::clamp(Style.GridPlane, 0, 2);
            CameraMatrix PlaneFromClip {};
            for (int Column = 0; Column < 4; ++Column)
            {
                for (int Row = 0; Row < 3; ++Row)
                {
                    PlaneFromClip[Column * 4 + Row] = Frame.WorldFromClip[Column * 4 + PlaneAxes[Plane][Row]];
                }
                PlaneFromClip[Column * 4 + 3] = Frame.WorldFromClip[Column * 4 + 3];
            }
            SketcherGridBlock Grid;
            Narrow(PlaneFromClip, Grid.PlaneFromClip);
            for (int Channel = 0; Channel < 4; ++Channel)
            {
                Grid.AlongU[Channel] = AxisColours[PlaneAxes[Plane][0]][Channel];
                Grid.AlongV[Channel] = AxisColours[PlaneAxes[Plane][1]][Channel];
            }
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, GridPipeline);
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0u, sizeof(Grid), &Grid);
            vkCmdDraw(Commands, 3u, 1u, 0u, 0u);
        };

        if (Style.XRayEnabled)
        {
            DrawPlanes();
            DrawGrid();
            DrawSurfaces(SurfaceDepthPipeline);
            DrawSurfaces(SurfaceGhostPipeline);
            DrawEdges(EdgeOccludedPipeline, EdgeOccludedIdentityPipeline, -float(OccludedMarginPixels * Frame.WorldPerPixel));
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }
        else if (Style.SectionEnabled)
        {
            DrawPlanes();
            DrawGrid();
            DrawSurfaceSpan(SurfacePipeline, 0u, SolidCornerCount, 0.0f);
            DrawLiveCutters();
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }
        else
        {
            DrawSurfaceSpan(SurfacePipeline, 0u, SolidCornerCount, 0.0f);
            DrawPlanes();
            DrawGrid();
            DrawLiveCutters();
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }

        if (GizmoCount[PaneIdx] > 0u && GizmoPipeline != VK_NULL_HANDLE)
        {
            VkClearAttachment DepthClear {};
            DepthClear.aspectMask              = VK_IMAGE_ASPECT_DEPTH_BIT;
            DepthClear.clearValue.depthStencil = { 1.0f, 0u };
            const VkClearRect PaneClear { Scissor, 0u, 1u };
            vkCmdClearAttachments(Commands, 1u, &DepthClear, 1u, &PaneClear);
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoPipeline);
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0u, sizeof(Projection), &Projection);
            vkCmdBindVertexBuffers(Commands, 0u, 1u, &GizmoBuffer.BufferHandle, &Origin);
            vkCmdDraw(Commands, GizmoCount[PaneIdx], 1u, GizmoFirst[PaneIdx], 0u);
        }
    }

    vkCmdEndRenderPass(Commands);

    TransitionImageLayout(Commands, Identity.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    static_cast<uint32_t*>(ProbeReadback.HostAddress)[0] = 0u;
    if (ProbeX >= 0 && ProbeY >= 0 && uint32_t(ProbeX) < Identity.WidthPx && uint32_t(ProbeY) < Identity.HeightPx)
    {
        VkBufferImageCopy Probe {};
        Probe.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        Probe.imageOffset      = { ProbeX, ProbeY, 0 };
        Probe.imageExtent      = { 1u, 1u, 1u };
        vkCmdCopyImageToBuffer(Commands, Identity.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ProbeReadback.BufferHandle, 1u, &Probe);
    }
    if (ReadbackCondition)
    {
        VkBufferImageCopy Whole {};
        Whole.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        Whole.imageExtent      = { Colour.WidthPx, Colour.HeightPx, 1u };
        vkCmdCopyImageToBuffer(Commands, Identity.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, IdentityReadback.BufferHandle, 1u, &Whole);
        TransitionImageLayout(Commands, Colour.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(Commands, Colour.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ColourReadback.BufferHandle, 1u, &Whole);
        TransitionImageLayout(Commands, Colour.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    else
    {
        TransitionImageLayout(Commands, Colour.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

uint32_t SketcherRasterization::ProbedIdentity() const
{
    return ProbeReadback.HostAddress ? static_cast<const uint32_t*>(ProbeReadback.HostAddress)[0] : 0u;
}

const uint32_t* SketcherRasterization::ReadbackColour() const
{
    return static_cast<const uint32_t*>(ColourReadback.HostAddress);
}

const uint32_t* SketcherRasterization::ReadbackIdentity() const
{
    return static_cast<const uint32_t*>(IdentityReadback.HostAddress);
}

} // namespace Frontier::Workspaces::Sketcher
