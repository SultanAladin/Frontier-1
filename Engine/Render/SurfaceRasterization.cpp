/*==========================================================================================================================================
                                                        SURFACERASTERIZATION.CPP
==========================================================================================================================================*/
// 🧩 Device-local geometry, depth-tested surfaces and expanded topology strokes; the caller owns submission and retirement.

#include "SurfaceRasterization.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace Frontier
{
namespace
{
void RequireRasterSuccess(VkResult Result)
{
    if (Result != VK_SUCCESS)
        throw std::runtime_error("Vulkan surface operation refused: " + std::to_string(int(Result)));
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEVICE ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceRasterization::RasterAllocation
{
    VulkanRasterAccess Access;
    std::shared_ptr<RasterAllocation> SharedPrograms;
    std::shared_ptr<RasterAllocation> SharedGeometry;
    std::shared_ptr<RasterAllocation> SampledMatCap;
    std::shared_ptr<RasterAllocation> SampledScattering;
    ~RasterAllocation()
    {
        Finalize();
    }
    VkExtent2D Extent{};
    // 📝 Optional sub-range of the shared index buffer; zero count draws the whole geometry. Targets over
    // the multi-shape inspection upload set this so only the selected specimen is rasterized.
    uint32_t DrawFirstIndex = 0;
    uint32_t DrawIndexCount = 0;
    bool TransparentBackground = false;
    bool MaterialCoordinatesEnabled = false;
    bool MaterialParametersEnabled = false;
    bool MaterialTexturesEnabled = false;
    uint32_t MaterialArrayLayers = 1;
    uint32_t MaterialParameterCount = 0;
    std::array<std::array<uint32_t,4>,8> MaterialParameterSelections{};
    std::array<std::shared_ptr<RasterAllocation>,4> MaterialImages;
    std::array<std::shared_ptr<RasterAllocation>,6> AuthoringMapImages;
    std::shared_ptr<RasterAllocation> EnvironmentImage;
    MaterialTexturePyramid EnvironmentPyramid;
    std::vector<VkBufferImageCopy> MaterialMipCopies;
    VkDescriptorSetLayout MaterialSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout MaterialLayout = VK_NULL_HANDLE;
    VkPipeline MaterialSurface = VK_NULL_HANDLE;
    VkSampler MaterialSampler = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkPhysicalDeviceProperties Limits{};
    struct BufferFootprint
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Bytes = 0;
    };
    struct ImageFootprint
    {
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
    };
    BufferFootprint Corners, Indices, Edges, CornerTransfer, IndexTransfer, EdgeTransfer;
    ImageFootprint Colour, Depth, MatCapImageStorage;
    BufferFootprint MatCapTransfer;
    VkDescriptorSetLayout MatCapSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout MatCapLayout = VK_NULL_HANDLE;
    VkPipeline MatCapSurface = VK_NULL_HANDLE;
    VkSampler MatCapSampler = VK_NULL_HANDLE;
    VkDescriptorPool MatCapPool = VK_NULL_HANDLE;
    VkDescriptorSet MatCapDescriptor = VK_NULL_HANDLE;
    VkDescriptorSetLayout ScatteringSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PreviewPbrLayout = VK_NULL_HANDLE;
    VkPipeline PreviewPbrSurface = VK_NULL_HANDLE;
    VkDescriptorPool ScatteringPool = VK_NULL_HANDLE;
    VkDescriptorSet ScatteringDescriptor = VK_NULL_HANDLE;
    BufferFootprint ScatteringParameters;
    VkRenderPass Rasterization = VK_NULL_HANDLE;
    VkFramebuffer Target = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkPipeline Surface = VK_NULL_HANDLE;
    VkPipeline Stroke = VK_NULL_HANDLE;
    uint32_t IndexCount = 0;
    uint32_t EdgeCount = 0;
    bool TransferPending = true;
    bool TransferRecorded = false;
    uint64_t TransferRecordings = 0;

#define FRONTIER_RASTER_FUNCTIONS(Function) \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) \
    Function(AllocateMemory) Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory) \
    Function(CreateImage) Function(DestroyImage) Function(GetImageMemoryRequirements) Function(BindImageMemory) \
    Function(CreateImageView) Function(DestroyImageView) Function(CreateRenderPass) Function(DestroyRenderPass) \
    Function(CreateFramebuffer) Function(DestroyFramebuffer) Function(CreatePipelineLayout) Function(DestroyPipelineLayout) \
    Function(CreateShaderModule) Function(DestroyShaderModule) Function(CreateGraphicsPipelines) Function(DestroyPipeline) \
    Function(CmdCopyBuffer) Function(CmdPipelineBarrier) Function(CmdBeginRenderPass) Function(CmdEndRenderPass) \
    Function(CmdBindPipeline) Function(CmdBindVertexBuffers) Function(CmdBindIndexBuffer) Function(CmdPushConstants) \
    Function(CmdSetViewport) Function(CmdSetScissor) Function(CmdDrawIndexed) Function(CmdDraw) \
    Function(CreateDescriptorSetLayout) Function(DestroyDescriptorSetLayout) Function(CreateDescriptorPool) \
    Function(DestroyDescriptorPool) Function(AllocateDescriptorSets) Function(UpdateDescriptorSets) \
    Function(CreateSampler) Function(DestroySampler) Function(CmdCopyBufferToImage) Function(CmdBindDescriptorSets)
#define FRONTIER_RASTER_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_RASTER_FUNCTIONS(FRONTIER_RASTER_POINTER)
#undef FRONTIER_RASTER_POINTER

    uint32_t ResolveMemory(uint32_t Allowed, VkMemoryPropertyFlags Required)
    {
        for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
            if ((Allowed & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Required) == Required)
                return Index;
        throw std::runtime_error("Required Vulkan surface memory is unavailable.");
    }

    void ConstructBuffer(
        BufferFootprint      &Footprint,
        VkDeviceSize          Bytes,
        VkBufferUsageFlags    Usage,
        VkMemoryPropertyFlags Properties)
    {
        Footprint.Bytes = Bytes;
        VkBufferCreateInfo Declaration{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Declaration.size = Bytes;
        Declaration.usage = Usage;
        Declaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        RequireRasterSuccess(CreateBuffer(Access.Device, &Declaration, nullptr, &Footprint.Buffer));
        VkMemoryRequirements Requirements;
        GetBufferMemoryRequirements(Access.Device, Footprint.Buffer, &Requirements);
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = ResolveMemory(Requirements.memoryTypeBits, Properties);
        RequireRasterSuccess(AllocateMemory(Access.Device, &Allocation, nullptr, &Footprint.Memory));
        RequireRasterSuccess(BindBufferMemory(Access.Device, Footprint.Buffer, Footprint.Memory, 0));
    }

    void ConstructGeometry(
        BufferFootprint   &Resident,
        BufferFootprint   &Transfer,
        const void        *Source,
        VkDeviceSize       Bytes,
        VkBufferUsageFlags Usage)
    {
        ConstructBuffer(Resident, Bytes, Usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ConstructBuffer(Transfer, Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireRasterSuccess(MapMemory(Access.Device, Transfer.Memory, 0, Bytes, 0, &Mapped));
        std::memcpy(Mapped, Source, static_cast<size_t>(Bytes));
        UnmapMemory(Access.Device, Transfer.Memory);
    }

    void ConstructImage(
        ImageFootprint    &Footprint,
        VkFormat           Format,
        VkImageUsageFlags  Usage,
        VkImageAspectFlags Aspect,
        uint32_t MipCount = 1,
        uint32_t ArrayLayers = 1,
        bool ArrayViewEnabled = false)
    {
        VkImageCreateInfo Declaration{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        Declaration.imageType = VK_IMAGE_TYPE_2D;
        Declaration.format = Format;
        Declaration.extent = {Extent.width, Extent.height, 1};
        Declaration.mipLevels = MipCount;
        Declaration.arrayLayers = ArrayLayers;
        Declaration.samples = VK_SAMPLE_COUNT_1_BIT;
        Declaration.tiling = VK_IMAGE_TILING_OPTIMAL;
        Declaration.usage = Usage;
        Declaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Declaration.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        RequireRasterSuccess(CreateImage(Access.Device, &Declaration, nullptr, &Footprint.Image));
        VkMemoryRequirements Requirements;
        GetImageMemoryRequirements(Access.Device, Footprint.Image, &Requirements);
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = ResolveMemory(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RequireRasterSuccess(AllocateMemory(Access.Device, &Allocation, nullptr, &Footprint.Memory));
        RequireRasterSuccess(BindImageMemory(Access.Device, Footprint.Image, Footprint.Memory, 0));
        VkImageViewCreateInfo View{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        View.image = Footprint.Image;
        View.viewType = ArrayViewEnabled ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        View.format = Format;
        View.subresourceRange = {Aspect, 0, MipCount, 0, ArrayLayers};
        RequireRasterSuccess(CreateImageView(Access.Device, &View, nullptr, &Footprint.View));
    }

    VkShaderModule ConstructShader(const RasterShader &Shader)
    {
        if (Shader.Words.size() < 5 || Shader.Words.size() > 4194304 || Shader.Words[0] != 0x07230203 ||
            !Shader.Entry || !*Shader.Entry)
            throw std::runtime_error("Expected a bounded SPIR-V shader and explicit entry point.");
        VkShaderModuleCreateInfo Declaration{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        Declaration.codeSize = Shader.Words.size_bytes();
        Declaration.pCode = Shader.Words.data();
        VkShaderModule Program = VK_NULL_HANDLE;
        RequireRasterSuccess(CreateShaderModule(Access.Device, &Declaration, nullptr, &Program));
        return Program;
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                     RASTERIZATION PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

    void ConstructPipeline(
        VkPipeline         &Program,
        const RasterShader &Vertex,
        const RasterShader &Fragment,
        bool                EdgesEnabled,
        bool                MatCapEnabled = false,
        bool                PreviewPbrEnabled = false,
        bool                MaterialEnabled = false)
    {
        const auto VertexProgram = ConstructShader(Vertex);
        VkShaderModule FragmentProgram = VK_NULL_HANDLE;
        try
        {
            FragmentProgram = ConstructShader(Fragment);
            VkPipelineShaderStageCreateInfo Programs[2]{};
            Programs[0].sType = Programs[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            Programs[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            Programs[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            Programs[0].module = VertexProgram;
            Programs[1].module = FragmentProgram;
            Programs[0].pName = Vertex.Entry;
            Programs[1].pName = Fragment.Entry;
            VkVertexInputBindingDescription Input{0, EdgesEnabled ? uint32_t(sizeof(SurfaceEdge)) : uint32_t(sizeof(SurfaceCorner)), EdgesEnabled ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
            VkVertexInputAttributeDescription Attributes[] = {
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                {2,0,VK_FORMAT_R32G32_SFLOAT,uint32_t(offsetof(SurfaceCorner,Coordinates))},
                {3,0,VK_FORMAT_R32G32B32A32_SFLOAT,uint32_t(offsetof(SurfaceCorner,Tangent))}};
            VkPipelineVertexInputStateCreateInfo VertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VertexInput.vertexBindingDescriptionCount = 1;
            VertexInput.pVertexBindingDescriptions = &Input;
            VertexInput.vertexAttributeDescriptionCount = MaterialEnabled ? 4 : 2;
            VertexInput.pVertexAttributeDescriptions = Attributes;
            VkPipelineInputAssemblyStateCreateInfo Assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo Viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            Viewport.viewportCount = Viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo Raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            Raster.polygonMode = VK_POLYGON_MODE_FILL;
            Raster.cullMode = VK_CULL_MODE_NONE;
            Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            Raster.lineWidth = 1;
            // 📝 Positive bias moves the fill away with LESS depth; the edge retains its actual surface depth.
            Raster.depthBiasEnable = !EdgesEnabled && !MatCapEnabled && !PreviewPbrEnabled && !MaterialEnabled;
            Raster.depthBiasConstantFactor = 1;
            Raster.depthBiasSlopeFactor = 1;
            VkPipelineMultisampleStateCreateInfo Samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            Samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo DepthTest{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            DepthTest.depthTestEnable = VK_TRUE;
            DepthTest.depthWriteEnable = !EdgesEnabled;
            DepthTest.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState Blend{};
            Blend.colorWriteMask = 15;
            Blend.blendEnable = EdgesEnabled;
            Blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            Blend.colorBlendOp = VK_BLEND_OP_ADD;
            Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            Blend.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo Colours{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            Colours.attachmentCount = 1;
            Colours.pAttachments = &Blend;
            VkDynamicState Dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo DynamicConfiguration{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            DynamicConfiguration.dynamicStateCount = 2;
            DynamicConfiguration.pDynamicStates = Dynamic;
            VkGraphicsPipelineCreateInfo Declaration{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            Declaration.stageCount = 2;
            Declaration.pStages = Programs;
            Declaration.pVertexInputState = &VertexInput;
            Declaration.pInputAssemblyState = &Assembly;
            Declaration.pViewportState = &Viewport;
            Declaration.pRasterizationState = &Raster;
            Declaration.pMultisampleState = &Samples;
            Declaration.pDepthStencilState = &DepthTest;
            Declaration.pColorBlendState = &Colours;
            Declaration.pDynamicState = &DynamicConfiguration;
            Declaration.layout = MaterialEnabled ? MaterialLayout : (PreviewPbrEnabled ? PreviewPbrLayout : (MatCapEnabled ? MatCapLayout : Layout));
            Declaration.renderPass = Rasterization;
            RequireRasterSuccess(CreateGraphicsPipelines(Access.Device, VK_NULL_HANDLE, 1, &Declaration, nullptr, &Program));
        }
        catch (...)
        {
            if (FragmentProgram) DestroyShaderModule(Access.Device, FragmentProgram, nullptr);
            DestroyShaderModule(Access.Device, VertexProgram, nullptr);
            throw;
        }
        DestroyShaderModule(Access.Device, FragmentProgram, nullptr);
        DestroyShaderModule(Access.Device, VertexProgram, nullptr);
    }

    void ConstructRasterization()
    {
        VkAttachmentDescription Attachments[2]{};
        Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        Attachments[1].format = VK_FORMAT_D32_SFLOAT;
        for (auto &Attachment : Attachments)
        {
            Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            Attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            Attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            Attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        Attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ColourTarget{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference DepthTarget{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription Subdivision{};
        Subdivision.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subdivision.colorAttachmentCount = 1;
        Subdivision.pColorAttachments = &ColourTarget;
        Subdivision.pDepthStencilAttachment = &DepthTarget;
        VkSubpassDependency Dependencies[2]{};
        Dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        Dependencies[0].dstSubpass = 0;
        Dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        Dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Dependencies[1].srcSubpass = 0;
        Dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        Dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo Declaration{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        Declaration.attachmentCount = 2;
        Declaration.pAttachments = Attachments;
        Declaration.subpassCount = 1;
        Declaration.pSubpasses = &Subdivision;
        Declaration.dependencyCount = 2;
        Declaration.pDependencies = Dependencies;
        RequireRasterSuccess(CreateRenderPass(Access.Device, &Declaration, nullptr, &Rasterization));
    }

    void ConstructTarget()
    {
        ConstructImage(Colour, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT);
        ConstructImage(Depth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        VkImageView Views[] = {Colour.View, Depth.View};
        VkFramebufferCreateInfo TargetDeclaration{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        TargetDeclaration.renderPass = Rasterization;
        TargetDeclaration.attachmentCount = 2;
        TargetDeclaration.pAttachments = Views;
        TargetDeclaration.width = Extent.width;
        TargetDeclaration.height = Extent.height;
        TargetDeclaration.layers = 1;
        RequireRasterSuccess(CreateFramebuffer(Access.Device, &TargetDeclaration, nullptr, &Target));
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATCAP PROGRAM AND TEXTURE RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

    void ConstructTextureInterface()
    {
        VkDescriptorSetLayoutBinding Bindings[2]{};
        Bindings[0] = {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        Bindings[1] = {1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo Declaration{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        Declaration.bindingCount = 2;
        Declaration.pBindings = Bindings;
        RequireRasterSuccess(CreateDescriptorSetLayout(Access.Device,&Declaration,nullptr,&MatCapSetLayout));
        VkSamplerCreateInfo Sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        Sampler.magFilter = Sampler.minFilter = VK_FILTER_LINEAR;
        Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        Sampler.addressModeU = Sampler.addressModeV = Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Sampler.maxAnisotropy = 1;
        RequireRasterSuccess(CreateSampler(Access.Device,&Sampler,nullptr,&MatCapSampler));
    }

    void ConstructMatCapPrograms(const SurfaceRasterShaders &Shaders)
    {
        VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(MatCapProjection)};
        VkPipelineLayoutCreateInfo LayoutDeclaration{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        LayoutDeclaration.setLayoutCount = 1;
        LayoutDeclaration.pSetLayouts = &MatCapSetLayout;
        LayoutDeclaration.pushConstantRangeCount = 1;
        LayoutDeclaration.pPushConstantRanges = &Range;
        RequireRasterSuccess(CreatePipelineLayout(Access.Device,&LayoutDeclaration,nullptr,&MatCapLayout));
        ConstructPipeline(MatCapSurface,Shaders.MatCapVertex,Shaders.MatCapFragment,false,true);
    }

    void ConstructPreviewPbrPrograms(const SurfaceRasterShaders &Shaders)
    {
        if (Limits.limits.maxPushConstantsSize < sizeof(SurfaceNormalProjection) ||
            Limits.limits.maxUniformBufferRange < sizeof(PreviewScattering))
            throw std::runtime_error("Preview scattering exceeds device parameter capacity.");
        VkDescriptorSetLayoutBinding Input{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,
                                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo Declaration{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        Declaration.bindingCount = 1;
        Declaration.pBindings = &Input;
        RequireRasterSuccess(CreateDescriptorSetLayout(Access.Device,&Declaration,nullptr,&ScatteringSetLayout));
        const VkDescriptorSetLayout Sets[]{MatCapSetLayout,ScatteringSetLayout};
        const VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(SurfaceNormalProjection)};
        VkPipelineLayoutCreateInfo LayoutDeclaration{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        LayoutDeclaration.setLayoutCount = 2;
        LayoutDeclaration.pSetLayouts = Sets;
        LayoutDeclaration.pushConstantRangeCount = 1;
        LayoutDeclaration.pPushConstantRanges = &Range;
        RequireRasterSuccess(CreatePipelineLayout(Access.Device,&LayoutDeclaration,nullptr,&PreviewPbrLayout));
        ConstructPipeline(PreviewPbrSurface,Shaders.PreviewPbrVertex,Shaders.PreviewPbrFragment,false,false,true);
    }

    template<typename ScatteringSpecification>
    void ConstructScatteringParameters(const ScatteringSpecification &Scattering)
    {
        ConstructBuffer(ScatteringParameters,sizeof(Scattering),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireRasterSuccess(MapMemory(Access.Device,ScatteringParameters.Memory,0,sizeof(Scattering),0,&Mapped));
        std::memcpy(Mapped,&Scattering,sizeof(Scattering));
        UnmapMemory(Access.Device,ScatteringParameters.Memory);
        VkDescriptorPoolSize Size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};
        VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        Pool.maxSets = 1;
        Pool.poolSizeCount = 1;
        Pool.pPoolSizes = &Size;
        RequireRasterSuccess(CreateDescriptorPool(Access.Device,&Pool,nullptr,&ScatteringPool));
        VkDescriptorSetAllocateInfo Allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocation.descriptorPool = ScatteringPool;
        Allocation.descriptorSetCount = 1;
        Allocation.pSetLayouts = &SharedPrograms->ScatteringSetLayout;
        RequireRasterSuccess(AllocateDescriptorSets(Access.Device,&Allocation,&ScatteringDescriptor));
        VkDescriptorBufferInfo Buffer{ScatteringParameters.Buffer,0,sizeof(Scattering)};
        VkWriteDescriptorSet Write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        Write.dstSet = ScatteringDescriptor;
        Write.dstBinding = 0;
        Write.descriptorCount = 1;
        Write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        Write.pBufferInfo = &Buffer;
        UpdateDescriptorSets(Access.Device,1,&Write,0,nullptr);
    }

    void ConstructMaterialPrograms(const SurfaceRasterShaders &Shaders)
    {
        if (Limits.limits.maxPushConstantsSize < sizeof(SurfaceNormalProjection) ||
            Limits.limits.maxUniformBufferRange < sizeof(MaterialScattering))
            throw std::runtime_error("Material parameter capacity unavailable.");
        VkDescriptorSetLayoutBinding Inputs[]{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                                             {1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                                             {2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,6,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                                             {3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo Declaration{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        Declaration.bindingCount = 4;
        Declaration.pBindings = Inputs;
        RequireRasterSuccess(CreateDescriptorSetLayout(Access.Device,&Declaration,nullptr,&MaterialSetLayout));
        if (!ScatteringSetLayout)
        {
            VkDescriptorSetLayoutBinding Uniform{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,
                                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
            Declaration.bindingCount = 1;
            Declaration.pBindings = &Uniform;
            RequireRasterSuccess(CreateDescriptorSetLayout(Access.Device,&Declaration,nullptr,&ScatteringSetLayout));
        }
        const VkDescriptorSetLayout Sets[]{MaterialSetLayout,ScatteringSetLayout};
        const VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(SurfaceNormalProjection)};
        VkPipelineLayoutCreateInfo LayoutDeclaration{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        LayoutDeclaration.setLayoutCount = 2;
        LayoutDeclaration.pSetLayouts = Sets;
        LayoutDeclaration.pushConstantRangeCount = 1;
        LayoutDeclaration.pPushConstantRanges = &Range;
        RequireRasterSuccess(CreatePipelineLayout(Access.Device,&LayoutDeclaration,nullptr,&MaterialLayout));
        VkSamplerCreateInfo Sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        Sampler.magFilter = Sampler.minFilter = VK_FILTER_LINEAR;
        Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        Sampler.addressModeU = Sampler.addressModeV = Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        Sampler.maxLod = 12;
        Sampler.maxAnisotropy = 1;
        RequireRasterSuccess(CreateSampler(Access.Device,&Sampler,nullptr,&MaterialSampler));
        ConstructPipeline(MaterialSurface,Shaders.MaterialVertex,Shaders.MaterialFragment,false,false,false,true);
    }

    void ConstructMaterialImage(const MaterialTexturePyramid &Texture)
    {
        const bool FloatSamples = !Texture.FloatRgba.empty();
        const bool Srgb = !FloatSamples &&
            (Texture.Category == MaterialTextureCategory::Albedo || Texture.Category == MaterialTextureCategory::Radiance);
        const VkFormat Format = FloatSamples ? VK_FORMAT_R32G32B32A32_SFLOAT :
            Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        VkFormatProperties Properties;
        Access.QueryFormat(Access.PhysicalDevice,Format,&Properties);
        constexpr auto Required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((Properties.optimalTilingFeatures & Required) != Required)
            throw std::runtime_error("Material texture format/filter/upload unsupported.");
        Extent = {Texture.Levels[0].Width,Texture.Levels[0].Height};
        ConstructImage(MatCapImageStorage,Format,VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT,uint32_t(Texture.Levels.size()),Texture.Layers,true);
        MaterialArrayLayers = Texture.Layers;
        const size_t Bytes = FloatSamples ? Texture.FloatRgba.size() * sizeof(float) : Texture.Rgba.size();
        const void *Pixels = FloatSamples ? static_cast<const void *>(Texture.FloatRgba.data()) :
            static_cast<const void *>(Texture.Rgba.data());
        ConstructBuffer(MatCapTransfer,Bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireRasterSuccess(MapMemory(Access.Device,MatCapTransfer.Memory,0,Bytes,0,&Mapped));
        std::memcpy(Mapped,Pixels,Bytes);
        UnmapMemory(Access.Device,MatCapTransfer.Memory);
        for (size_t Index = 0; Index < Texture.Levels.size(); ++Index)
        {
            const auto &Level = Texture.Levels[Index];
            VkBufferImageCopy Copy{};
            Copy.bufferOffset = Level.Offset;
            Copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,uint32_t(Index),0,Texture.Layers};
            Copy.imageExtent = {Level.Width,Level.Height,1};
            MaterialMipCopies.push_back(Copy);
        }
    }

    void ConstructMaterialTextures(const std::array<MaterialTexturePyramid,4> &Textures,
                                   const std::array<MaterialTexturePyramid,6> &AuthoringMaps)
    {
        VkDescriptorImageInfo Images[4]{};
        VkDescriptorImageInfo AuthoringImages[6]{};
        for (size_t Index = 0; Index < Textures.size(); ++Index)
        {
            auto Image = std::make_shared<RasterAllocation>();
            Image->InitializeDispatch(Access);
            Image->SharedPrograms = SharedPrograms;
            Image->ConstructMaterialImage(Textures[Index]);
            Images[Index].imageView = Image->MatCapImageStorage.View;
            Images[Index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            MaterialImages[Index] = std::move(Image);
        }
        for (size_t Index = 0; Index < AuthoringMaps.size(); ++Index)
        {
            auto Image = std::make_shared<RasterAllocation>();
            Image->InitializeDispatch(Access);
            Image->SharedPrograms = SharedPrograms;
            Image->ConstructMaterialImage(AuthoringMaps[Index]);
            AuthoringImages[Index].imageView = Image->MatCapImageStorage.View;
            AuthoringImages[Index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            AuthoringMapImages[Index] = std::move(Image);
        }
        {
            auto Image = std::make_shared<RasterAllocation>();
            Image->InitializeDispatch(Access);
            Image->SharedPrograms = SharedPrograms;
            // 📝 Neutral 2x2 fallback until the host configures the studio archive, so binding 3
            // is always a valid sampled array image.
            static const MaterialTexturePyramid FallbackEnvironment = [] {
                MaterialTexturePyramid Pyramid;
                Pyramid.Layers = 1;
                Pyramid.Category = MaterialTextureCategory::Radiance;
                Pyramid.Rgba = {96,96,96,255, 96,96,96,255, 64,64,64,255, 64,64,64,255};
                Pyramid.Levels.push_back({2,2,0});
                return Pyramid;
            }();
            const auto &Environment = SharedPrograms && !SharedPrograms->EnvironmentPyramid.Levels.empty() ?
                SharedPrograms->EnvironmentPyramid : FallbackEnvironment;
            Image->ConstructMaterialImage(Environment);
            EnvironmentImage = std::move(Image);
        }
        const VkDescriptorPoolSize Sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,11},{VK_DESCRIPTOR_TYPE_SAMPLER,1}};
        VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        Pool.maxSets = 1;
        Pool.poolSizeCount = 2;
        Pool.pPoolSizes = Sizes;
        RequireRasterSuccess(CreateDescriptorPool(Access.Device,&Pool,nullptr,&MatCapPool));
        VkDescriptorSetAllocateInfo Allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocation.descriptorPool = MatCapPool;
        Allocation.descriptorSetCount = 1;
        Allocation.pSetLayouts = &SharedPrograms->MaterialSetLayout;
        RequireRasterSuccess(AllocateDescriptorSets(Access.Device,&Allocation,&MatCapDescriptor));
        VkDescriptorImageInfo Sampler{SharedPrograms->MaterialSampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
        VkDescriptorImageInfo EnvironmentInfo{};
        EnvironmentInfo.imageView = EnvironmentImage->MatCapImageStorage.View;
        EnvironmentInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet Writes[4]{};
        for (auto &Write : Writes)
        {
            Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Write.dstSet = MatCapDescriptor;
        }
        Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        Writes[0].descriptorCount = 4;
        Writes[0].pImageInfo = Images;
        Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        Writes[1].dstBinding = 1;
        Writes[1].descriptorCount = 1;
        Writes[1].pImageInfo = &Sampler;
        Writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        Writes[2].dstBinding = 2;
        Writes[2].descriptorCount = 6;
        Writes[2].pImageInfo = AuthoringImages;
        Writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        Writes[3].dstBinding = 3;
        Writes[3].descriptorCount = 1;
        Writes[3].pImageInfo = &EnvironmentInfo;
        UpdateDescriptorSets(Access.Device,4,Writes,0,nullptr);
        MaterialTexturesEnabled = true;
    }

    void ConstructMatCapImage(const MatCapImage &Image)
    {
        VkFormatProperties Properties;
        Access.QueryFormat(Access.PhysicalDevice,VK_FORMAT_R8G8B8A8_UNORM,&Properties);
        constexpr auto Required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((Properties.optimalTilingFeatures & Required) != Required)
            throw std::runtime_error("Required MatCap sampling/upload format features unavailable.");
        ConstructImage(MatCapImageStorage,VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        ConstructBuffer(MatCapTransfer,Image.Rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireRasterSuccess(MapMemory(Access.Device,MatCapTransfer.Memory,0,Image.Rgba.size(),0,&Mapped));
        std::memcpy(Mapped,Image.Rgba.data(),Image.Rgba.size());
        UnmapMemory(Access.Device,MatCapTransfer.Memory);
        const VkDescriptorPoolSize Sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_SAMPLER,1}};
        VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        Pool.maxSets = 1;
        Pool.poolSizeCount = 2;
        Pool.pPoolSizes = Sizes;
        RequireRasterSuccess(CreateDescriptorPool(Access.Device,&Pool,nullptr,&MatCapPool));
        VkDescriptorSetAllocateInfo Allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocation.descriptorPool = MatCapPool;
        Allocation.descriptorSetCount = 1;
        Allocation.pSetLayouts = &SharedPrograms->MatCapSetLayout;
        RequireRasterSuccess(AllocateDescriptorSets(Access.Device,&Allocation,&MatCapDescriptor));
        VkDescriptorImageInfo Images[2]{};
        Images[0].imageView = MatCapImageStorage.View;
        Images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Images[1].sampler = SharedPrograms->MatCapSampler;
        VkWriteDescriptorSet Writes[2]{};
        for (uint32_t Index = 0; Index < 2; ++Index)
        {
            Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[Index].dstSet = MatCapDescriptor;
            Writes[Index].dstBinding = Index;
            Writes[Index].descriptorCount = 1;
            Writes[Index].descriptorType = Sizes[Index].type;
            Writes[Index].pImageInfo = &Images[Index];
        }
        UpdateDescriptorSets(Access.Device,2,Writes,0,nullptr);
    }

    void ConstructMatCapTransfer(VkCommandBuffer Commands)
    {
        VkImageMemoryBarrier Barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        Barrier.srcAccessMask = 0;
        Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.srcQueueFamilyIndex = Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image = MatCapImageStorage.Image;
        Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,MaterialMipCopies.empty() ? 1u : uint32_t(MaterialMipCopies.size()),0,MaterialArrayLayers};
        CmdPipelineBarrier(Commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,0,nullptr,0,nullptr,1,&Barrier);
        VkBufferImageCopy Region{};
        Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        Region.imageExtent = {Extent.width,Extent.height,1};
        CmdCopyBufferToImage(Commands,MatCapTransfer.Buffer,MatCapImageStorage.Image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,MaterialMipCopies.empty() ? 1u : uint32_t(MaterialMipCopies.size()),
                             MaterialMipCopies.empty() ? &Region : MaterialMipCopies.data());
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        CmdPipelineBarrier(Commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0,0,nullptr,0,nullptr,1,&Barrier);
        TransferRecorded = true;
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                     COMMAND RECORDING
//------------------------------------------------------------------------------------------------------------------------

    void ConstructCommands(
        VkCommandBuffer          Commands,
        const SurfaceProjection *Projection,
        const MatCapProjection  *MatCapParameters,
        const std::shared_ptr<RasterAllocation> &MatCap,
        const std::shared_ptr<RasterAllocation> &Scattering = {})
    {
        if (!Target || !Commands) throw std::runtime_error("Surface target or command buffer is unavailable.");
        auto &Geometry = *SharedGeometry;
        if (Geometry.TransferPending)
        {
            for (const auto &Pair : {std::pair{&Geometry.Corners, &Geometry.CornerTransfer}, std::pair{&Geometry.Indices, &Geometry.IndexTransfer},
                                    std::pair{&Geometry.Edges, &Geometry.EdgeTransfer}})
            {
                if (!Pair.first->Buffer) continue;
                VkBufferCopy Region{0, 0, Pair.first->Bytes};
                CmdCopyBuffer(Commands, Pair.second->Buffer, Pair.first->Buffer, 1, &Region);
            }
            VkMemoryBarrier Barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                               0, 1, &Barrier, 0, nullptr, 0, nullptr);
            Geometry.TransferRecorded = true;
        }
        SampledMatCap = MatCap;
        SampledScattering = Scattering;
        if (SampledMatCap)
        {
            if (SampledMatCap->MaterialTexturesEnabled)
            {
                for (const auto &Image : SampledMatCap->MaterialImages)
                    if (Image->TransferPending) Image->ConstructMatCapTransfer(Commands);
                for (const auto &Image : SampledMatCap->AuthoringMapImages)
                    if (Image->TransferPending) Image->ConstructMatCapTransfer(Commands);
                if (SampledMatCap->EnvironmentImage && SampledMatCap->EnvironmentImage->TransferPending)
                    SampledMatCap->EnvironmentImage->ConstructMatCapTransfer(Commands);
            }
            else if (SampledMatCap->TransferPending) SampledMatCap->ConstructMatCapTransfer(Commands);
        }
        VkClearValue Clear[2]{};
        Clear[0].color = TransparentBackground ? VkClearColorValue{{0, 0, 0, 0}} : VkClearColorValue{{0.012f, 0.015f, 0.017f, 1}};
        Clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo Declaration{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        Declaration.renderPass = Rasterization;
        Declaration.framebuffer = Target;
        Declaration.renderArea.extent = Extent;
        Declaration.clearValueCount = 2;
        Declaration.pClearValues = Clear;
        CmdBeginRenderPass(Commands, &Declaration, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport Viewport{0, 0, float(Extent.width), float(Extent.height), 0, 1};
        VkRect2D Scissor{{0, 0}, Extent};
        CmdSetViewport(Commands, 0, 1, &Viewport);
        CmdSetScissor(Commands, 0, 1, &Scissor);
        const bool MaterialEnabled = MatCap && MatCap->MaterialTexturesEnabled;
        const auto ActiveLayout = MaterialEnabled ? SharedPrograms->MaterialLayout : Scattering ? SharedPrograms->PreviewPbrLayout : (MatCap ? SharedPrograms->MatCapLayout : Layout);
        if (MatCap)
        {
            CmdPushConstants(Commands, ActiveLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(*MatCapParameters), MatCapParameters);
            CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ActiveLayout, 0, 1,
                                  &MatCap->MatCapDescriptor, 0, nullptr);
            if (Scattering)
                CmdBindDescriptorSets(Commands,VK_PIPELINE_BIND_POINT_GRAPHICS,ActiveLayout,1,1,
                                      &Scattering->ScatteringDescriptor,0,nullptr);
        }
        else
        {
            auto Projected = *Projection;
            Projected.DisplayStroke[0] = float(Extent.width);
            Projected.DisplayStroke[1] = float(Extent.height);
            CmdPushConstants(Commands, Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(Projected), &Projected);
        }
        constexpr VkDeviceSize Offset = 0;
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, MaterialEnabled ? SharedPrograms->MaterialSurface : (Scattering ? SharedPrograms->PreviewPbrSurface : (MatCap ? SharedPrograms->MatCapSurface : Surface)));
        CmdBindVertexBuffers(Commands, 0, 1, &Geometry.Corners.Buffer, &Offset);
        CmdBindIndexBuffer(Commands, Geometry.Indices.Buffer, 0, VK_INDEX_TYPE_UINT32);
        if (DrawIndexCount && uint64_t(DrawFirstIndex) + DrawIndexCount > Geometry.IndexCount)
            throw std::runtime_error("Configured index range exceeds the shared geometry.");
        CmdDrawIndexed(Commands, DrawIndexCount ? DrawIndexCount : Geometry.IndexCount, 1,
                       DrawIndexCount ? DrawFirstIndex : 0, 0, 0);
        if (Geometry.EdgeCount && !MatCap)
        {
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Stroke);
            CmdBindVertexBuffers(Commands, 0, 1, &Geometry.Edges.Buffer, &Offset);
            CmdDraw(Commands, 6, Geometry.EdgeCount, 0, 0);
        }
        CmdEndRenderPass(Commands);
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                     COMPLETED RESOURCE RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

    void Finalize() noexcept
    {
        if (!Access.Device) return;
        if (Target) DestroyFramebuffer(Access.Device, Target, nullptr);
        if (!SharedPrograms)
        {
            if (MaterialSurface) DestroyPipeline(Access.Device,MaterialSurface,nullptr);
            if (MaterialLayout) DestroyPipelineLayout(Access.Device,MaterialLayout,nullptr);
            if (MaterialSetLayout) DestroyDescriptorSetLayout(Access.Device,MaterialSetLayout,nullptr);
            if (MaterialSampler) DestroySampler(Access.Device,MaterialSampler,nullptr);
            if (PreviewPbrSurface) DestroyPipeline(Access.Device,PreviewPbrSurface,nullptr);
            if (PreviewPbrLayout) DestroyPipelineLayout(Access.Device,PreviewPbrLayout,nullptr);
            if (ScatteringSetLayout) DestroyDescriptorSetLayout(Access.Device,ScatteringSetLayout,nullptr);
            if (MatCapSurface) DestroyPipeline(Access.Device, MatCapSurface, nullptr);
            if (MatCapLayout) DestroyPipelineLayout(Access.Device, MatCapLayout, nullptr);
            if (MatCapSetLayout) DestroyDescriptorSetLayout(Access.Device, MatCapSetLayout, nullptr);
            if (Stroke) DestroyPipeline(Access.Device, Stroke, nullptr);
            if (Surface) DestroyPipeline(Access.Device, Surface, nullptr);
            if (Layout) DestroyPipelineLayout(Access.Device, Layout, nullptr);
            if (Rasterization) DestroyRenderPass(Access.Device, Rasterization, nullptr);
        }
        if (ScatteringPool) DestroyDescriptorPool(Access.Device,ScatteringPool,nullptr);
        if (MatCapPool) DestroyDescriptorPool(Access.Device, MatCapPool, nullptr);
        if (MatCapSampler) DestroySampler(Access.Device, MatCapSampler, nullptr);
        for (auto *Footprint : {&Colour, &Depth, &MatCapImageStorage})
        {
            if (Footprint->View) DestroyImageView(Access.Device, Footprint->View, nullptr);
            if (Footprint->Image) DestroyImage(Access.Device, Footprint->Image, nullptr);
            if (Footprint->Memory) FreeMemory(Access.Device, Footprint->Memory, nullptr);
            *Footprint = {};
        }
        for (auto *Footprint : {&Corners, &Indices, &Edges, &CornerTransfer, &IndexTransfer, &EdgeTransfer, &MatCapTransfer, &ScatteringParameters})
        {
            if (Footprint->Buffer) DestroyBuffer(Access.Device, Footprint->Buffer, nullptr);
            if (Footprint->Memory) FreeMemory(Access.Device, Footprint->Memory, nullptr);
            *Footprint = {};
        }
        MaterialSurface = VK_NULL_HANDLE;
        MaterialLayout = VK_NULL_HANDLE;
        MaterialSetLayout = VK_NULL_HANDLE;
        MaterialSampler = VK_NULL_HANDLE;
        MaterialImages = {};
        AuthoringMapImages = {};
        EnvironmentImage.reset();
        MaterialMipCopies.clear();
        PreviewPbrSurface = VK_NULL_HANDLE;
        PreviewPbrLayout = VK_NULL_HANDLE;
        ScatteringSetLayout = VK_NULL_HANDLE;
        ScatteringPool = VK_NULL_HANDLE;
        ScatteringDescriptor = VK_NULL_HANDLE;
        MatCapSurface = Stroke = Surface = VK_NULL_HANDLE;
        MatCapLayout = VK_NULL_HANDLE;
        MatCapSetLayout = VK_NULL_HANDLE;
        MatCapPool = VK_NULL_HANDLE;
        MatCapSampler = VK_NULL_HANDLE;
        MatCapDescriptor = VK_NULL_HANDLE;
        Target = VK_NULL_HANDLE;
        Rasterization = VK_NULL_HANDLE;
        Layout = VK_NULL_HANDLE;
        SampledScattering.reset();
        SampledMatCap.reset();
        SharedGeometry.reset();
        SharedPrograms.reset();
        Access = {};
        Extent = {};
    }

    void InitializeDispatch(const VulkanRasterAccess &DeviceAccess)
    {
#define FRONTIER_LOAD_RASTER(Name) \
        Name = reinterpret_cast<PFN_vk##Name>(DeviceAccess.ResolveDevice(DeviceAccess.Device, "vk" #Name)); \
        if (!Name) throw std::runtime_error("Vulkan surface entry point unavailable: vk" #Name);
        FRONTIER_RASTER_FUNCTIONS(FRONTIER_LOAD_RASTER)
#undef FRONTIER_LOAD_RASTER
        Access = DeviceAccess;
        Access.QueryMemory(Access.PhysicalDevice, &Memory);
        Access.QueryLimits(Access.PhysicalDevice, &Limits);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

SurfaceRasterization::SurfaceRasterization() : Allocation(std::make_unique<RasterAllocation>())
{
}
SurfaceRasterization::SurfaceRasterization(SurfaceRasterization &&Source) noexcept : Allocation(std::move(Source.Allocation))
{
    Source.Allocation = std::make_unique<RasterAllocation>();
}
SurfaceRasterization::~SurfaceRasterization()
{
    Finalize();
}
void SurfaceRasterization::ConfigureTransparentBackground(bool Enabled) noexcept
{
    if (Allocation) Allocation->TransparentBackground = Enabled;
}
void SurfaceRasterization::ConfigureIndexRange(uint32_t FirstIndex, uint32_t IndexCount) noexcept
{
    if (Allocation)
    {
        Allocation->DrawFirstIndex = FirstIndex;
        Allocation->DrawIndexCount = IndexCount;
    }
}

void SurfaceRasterization::Finalize() noexcept
{
    Allocation->Finalize();
}
void SurfaceRasterization::ConfirmSubmission() noexcept
{
    const auto Confirm = [](const auto &Residence)
    {
        if (Residence && Residence->TransferPending && Residence->TransferRecorded)
        {
            Residence->TransferPending = false;
            Residence->TransferRecorded = false;
            ++Residence->TransferRecordings;
        }
    };
    Confirm(Allocation->SharedGeometry);
    Confirm(Allocation->SampledMatCap);
    if (Allocation->SampledMatCap && Allocation->SampledMatCap->MaterialTexturesEnabled)
        for (const auto &Image : Allocation->SampledMatCap->MaterialImages) Confirm(Image);
    if (Allocation->SampledMatCap && Allocation->SampledMatCap->MaterialTexturesEnabled)
        for (const auto &Image : Allocation->SampledMatCap->AuthoringMapImages) Confirm(Image);
    if (Allocation->SampledMatCap && Allocation->SampledMatCap->MaterialTexturesEnabled)
        Confirm(Allocation->SampledMatCap->EnvironmentImage);
}
VkImage SurfaceRasterization::QueryImage() const noexcept
{
    return Allocation->Colour.Image;
}
VkImageView SurfaceRasterization::QueryImageView() const noexcept
{
    return Allocation->Colour.View;
}
VkExtent2D SurfaceRasterization::QueryExtent() const noexcept
{
    return Allocation->Extent;
}
uint64_t SurfaceRasterization::QueryGeometryUploadCount() const noexcept
{
    return Allocation->SharedGeometry ? Allocation->SharedGeometry->TransferRecordings : 0;
}
void SurfaceRasterization::ConstructCommands(VkCommandBuffer Commands, const SurfaceProjection &Projection)
{
    for (const auto Coefficient : Projection.ClipFromSurface)
        if (!std::isfinite(Coefficient)) throw std::runtime_error("Nonfinite surface projection refused.");
    if (!std::isfinite(Projection.DisplayStroke[2]) || Projection.DisplayStroke[2] < 0.25f || Projection.DisplayStroke[2] > 16)
        throw std::runtime_error("Surface stroke width must be within 0.25 to 16 render pixels.");
    for (const auto *Colour : {&Projection.SurfaceColour, &Projection.EdgeColour})
        for (const auto Channel : *Colour)
            if (!std::isfinite(Channel) || Channel < 0 || Channel > 1)
                throw std::runtime_error("CAD display colours must be finite normalized channels.");
    Allocation->ConstructCommands(Commands, &Projection, nullptr, {});
}
void SurfaceRasterization::ConstructCommands(
    VkCommandBuffer         Commands,
    const MatCapProjection &Projection,
    const SurfaceMatCap    &MatCap)
{
    if (!ValidateMatCapProjection(Projection)) throw std::runtime_error("Invalid MatCap projection or normalized normal transform.");
    if (!Allocation->SharedPrograms || !Allocation->SharedPrograms->MatCapSurface || !MatCap.Allocation ||
        MatCap.Allocation->Access.Device != Allocation->Access.Device)
        throw std::runtime_error("MatCap requires initialized compatible programs, target and resident texture.");
    Allocation->ConstructCommands(Commands, nullptr, &Projection, MatCap.Allocation);
}

void SurfaceRasterization::ConstructCommands(
    VkCommandBuffer                   Commands,
    const SurfaceNormalProjection    &Projection,
    const SurfaceTextureResidence    &Albedo,
    const SurfaceScatteringResidence &Scattering)
{
    if (!Allocation || !ValidateSurfaceNormalProjection(Projection) || !Allocation->SharedPrograms ||
        !Allocation->SharedPrograms->PreviewPbrSurface || !Albedo.Allocation || !Scattering.Allocation || Scattering.Allocation->MaterialParametersEnabled ||
        Albedo.Allocation->Access.Device != Allocation->Access.Device ||
        Scattering.Allocation->Access.Device != Allocation->Access.Device)
        throw std::runtime_error("Preview PBR requires valid projection, compatible texture and scattering parameters.");
    Allocation->ConstructCommands(Commands,nullptr,&Projection,Albedo.Allocation,Scattering.Allocation);
}

void SurfaceRasterization::ConstructCommands(
    VkCommandBuffer                   Commands,
    const SurfaceNormalProjection    &Projection,
    const SurfaceMaterialResidence   &Material,
    const SurfaceScatteringResidence &Parameters)
{
    if (!Allocation->SharedPrograms || !Allocation->SharedPrograms->MaterialSurface ||
        !Allocation->SharedGeometry->MaterialCoordinatesEnabled || !ValidateSurfaceNormalProjection(Projection) ||
        !Material.Allocation || !Parameters.Allocation || !Parameters.Allocation->MaterialParametersEnabled ||
        Material.Allocation->Access.Device != Allocation->Access.Device || Parameters.Allocation->Access.Device != Allocation->Access.Device)
        throw std::runtime_error("Material draw requires authored UV/tangents, compatible texture residence and material uniform.");
    for (size_t Index = 0; Index < Parameters.Allocation->MaterialParameterCount; ++Index)
        for (size_t Slot = 0; Slot < 3; ++Slot)
            if (Parameters.Allocation->MaterialParameterSelections[Index][Slot+1] >= Material.Allocation->MaterialImages[Slot]->MaterialArrayLayers)
                throw std::runtime_error("Material parameter texture slice exceeds resident array capacity.");
    Allocation->ConstructCommands(Commands,nullptr,&Projection,Material.Allocation,Parameters.Allocation);
}

void SurfaceRasterization::Initialize(
    const SurfaceRasterPrograms    &Programs,
    const SurfaceGeometryResidence &Geometry,
    uint32_t                        Width,
    uint32_t                        Height)
{
    if (Allocation->Access.Device) throw std::runtime_error("Surface target already initialized.");
    if (!Programs.Allocation || !Geometry.Allocation ||
        Geometry.Allocation->Access.Device != Programs.Allocation->Access.Device ||
        Geometry.Allocation->Access.PhysicalDevice != Programs.Allocation->Access.PhysicalDevice ||
        !Width || !Height || Width > 8192 || Height > 8192 ||
        Width > Programs.Allocation->Limits.limits.maxImageDimension2D ||
        Height > Programs.Allocation->Limits.limits.maxImageDimension2D ||
        Width > Programs.Allocation->Limits.limits.maxFramebufferWidth ||
        Height > Programs.Allocation->Limits.limits.maxFramebufferHeight)
        throw std::runtime_error("Expected matching initialized programs/geometry and a bounded nonzero target extent.");
    auto &Resident = *Allocation;
    try
    {
        Resident.InitializeDispatch(Programs.Allocation->Access);
        Resident.SharedPrograms = Programs.Allocation;
        Resident.SharedGeometry = Geometry.Allocation;
        Resident.Extent = {Width,Height};
        Resident.Rasterization = Programs.Allocation->Rasterization;
        Resident.Layout = Programs.Allocation->Layout;
        Resident.Surface = Programs.Allocation->Surface;
        Resident.Stroke = Programs.Allocation->Stroke;
        Resident.ConstructTarget();
    }
    catch (...)
    {
        Resident.Finalize(); throw;
    }
}

SurfaceRasterPrograms::SurfaceRasterPrograms() = default;
SurfaceRasterPrograms::~SurfaceRasterPrograms() = default;
void SurfaceRasterPrograms::Finalize() noexcept
{
    Allocation.reset();
}

void SurfaceRasterPrograms::ConfigureEnvironment(const MaterialTexturePyramid &Environment)
{
    if (!Allocation) throw std::runtime_error("Surface programs require initialization before environment configuration.");
    if (Environment.Levels.empty() || Environment.Layers == 0)
        throw std::runtime_error("Environment archive requires at least one layer and mip level.");
    Allocation->EnvironmentPyramid = Environment;
}
void SurfaceRasterPrograms::Initialize(const VulkanRasterAccess &Access, const SurfaceRasterShaders &Shaders)
{
    if (Allocation) throw std::runtime_error("Surface programs already initialized.");
    if (!Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryFormat || !Access.QueryLimits)
        throw std::runtime_error("Invalid borrowed surface device access.");
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Access);
    VkFormatProperties Properties;
    Access.QueryFormat(Access.PhysicalDevice, VK_FORMAT_D32_SFLOAT, &Properties);
    if (!(Properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("D32 depth attachments unavailable; surface initialization refused.");
    Access.QueryFormat(Access.PhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &Properties);
    constexpr auto Required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if ((Properties.optimalTilingFeatures & Required) != Required)
        throw std::runtime_error("Required colour attachment features unavailable.");
    Resident->ConstructRasterization();
    VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SurfaceProjection)};
    VkPipelineLayoutCreateInfo Declaration{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Declaration.pushConstantRangeCount = 1;
    Declaration.pPushConstantRanges = &Range;
    RequireRasterSuccess(Resident->CreatePipelineLayout(Access.Device, &Declaration, nullptr, &Resident->Layout));
    Resident->ConstructPipeline(Resident->Surface, Shaders.SurfaceVertex, Shaders.SurfaceFragment, false);
    Resident->ConstructPipeline(Resident->Stroke, Shaders.EdgeVertex, Shaders.EdgeFragment, true);
    const bool MatCapVertexPresent = !Shaders.MatCapVertex.Words.empty();
    const bool MatCapFragmentPresent = !Shaders.MatCapFragment.Words.empty();
    if (MatCapVertexPresent != MatCapFragmentPresent)
        throw std::runtime_error("MatCap requires both vertex and fragment archives.");
    const bool MaterialPresent = !Shaders.MaterialVertex.Words.empty();
    if (MaterialPresent != !Shaders.MaterialFragment.Words.empty())
        throw std::runtime_error("Material rendering requires both shader archives.");
    const bool PreviewPbrPresent = !Shaders.PreviewPbrVertex.Words.empty();
    if (PreviewPbrPresent != !Shaders.PreviewPbrFragment.Words.empty())
        throw std::runtime_error("Preview PBR requires both shader archives.");
    if (MatCapVertexPresent || PreviewPbrPresent || MaterialPresent) Resident->ConstructTextureInterface();
    if (PreviewPbrPresent) Resident->ConstructPreviewPbrPrograms(Shaders);
    if (MaterialPresent) Resident->ConstructMaterialPrograms(Shaders);
    if (MatCapVertexPresent)
    {
        if (Resident->Limits.limits.maxPushConstantsSize < sizeof(MatCapProjection))
            throw std::runtime_error("MatCap push constant capacity unavailable.");
        Resident->ConstructMatCapPrograms(Shaders);
    }
    Allocation = std::move(Resident);
}

SurfaceGeometryResidence::SurfaceGeometryResidence() = default;
SurfaceGeometryResidence::~SurfaceGeometryResidence() = default;
void SurfaceGeometryResidence::Finalize() noexcept
{
    Allocation.reset();
}
void SurfaceGeometryResidence::Initialize(const VulkanRasterAccess &Access, const SurfaceGeometry &Geometry)
{
    if (Allocation) throw std::runtime_error("Surface geometry already initialized.");
    if (!Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryFormat || !Access.QueryLimits)
        throw std::runtime_error("Invalid borrowed surface device access.");
    std::string Diagnostic;
    if (!ValidateSurfaceGeometry(Geometry, Diagnostic)) throw std::runtime_error(Diagnostic);
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Access);
    Resident->ConstructGeometry(Resident->Corners, Resident->CornerTransfer, Geometry.Corners.data(),
                                Geometry.Corners.size() * sizeof(SurfaceCorner), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Resident->ConstructGeometry(Resident->Indices, Resident->IndexTransfer, Geometry.TriangleCorners.data(),
                                Geometry.TriangleCorners.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!Geometry.Edges.empty())
        Resident->ConstructGeometry(Resident->Edges, Resident->EdgeTransfer, Geometry.Edges.data(),
                                    Geometry.Edges.size() * sizeof(SurfaceEdge), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Resident->MaterialCoordinatesEnabled = Geometry.MaterialCoordinatesEnabled;
    Resident->IndexCount = static_cast<uint32_t>(Geometry.TriangleCorners.size());
    Resident->EdgeCount = static_cast<uint32_t>(Geometry.Edges.size());
    Allocation = std::move(Resident);
}
SurfaceTextureResidence::SurfaceTextureResidence() = default;
SurfaceTextureResidence::~SurfaceTextureResidence() = default;
void SurfaceTextureResidence::Finalize() noexcept
{
    Allocation.reset();
}
uint64_t SurfaceTextureResidence::QueryUploadCount() const noexcept
{
    return Allocation ? Allocation->TransferRecordings : 0;
}
void SurfaceTextureResidence::Initialize(const SurfaceRasterPrograms &Programs, const MatCapImage &Image)
{
    if (Allocation || !Programs.Allocation || !Programs.Allocation->MatCapSetLayout || !ValidateMatCapImage(Image))
        throw std::runtime_error("Expected dormant MatCap texture, initialized MatCap programs and bounded RGBA pixels.");
    if (Image.Width > Programs.Allocation->Limits.limits.maxImageDimension2D ||
        Image.Height > Programs.Allocation->Limits.limits.maxImageDimension2D)
        throw std::runtime_error("MatCap image exceeds the physical device texture limit.");
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Programs.Allocation->Access);
    Resident->SharedPrograms = Programs.Allocation;
    Resident->Extent = {Image.Width,Image.Height};
    Resident->ConstructMatCapImage(Image);
    Allocation = std::move(Resident);
}
SurfaceScatteringResidence::SurfaceScatteringResidence() = default;
SurfaceScatteringResidence::~SurfaceScatteringResidence() = default;
void SurfaceScatteringResidence::Finalize() noexcept
{
    Allocation.reset();
}
void SurfaceScatteringResidence::Initialize(const SurfaceRasterPrograms &Programs,const PreviewScattering &Scattering)
{
    if (Allocation || !Programs.Allocation || !Programs.Allocation->PreviewPbrSurface || !ValidatePreviewScattering(Scattering))
        throw std::runtime_error("Invalid preview scattering sequence or dormant program contract.");
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Programs.Allocation->Access);
    Resident->SharedPrograms = Programs.Allocation;
    Resident->ConstructScatteringParameters(Scattering);
    Allocation = std::move(Resident);
}
void SurfaceScatteringResidence::Initialize(const SurfaceRasterPrograms &Programs,const MaterialScattering &Scattering)
{
    if (Allocation || !Programs.Allocation || !Programs.Allocation->MaterialSurface || !ValidateMaterialScattering(Scattering))
        throw std::runtime_error("Invalid material parameters or dormant shader contract.");
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Programs.Allocation->Access);
    Resident->SharedPrograms = Programs.Allocation;
    Resident->ConstructScatteringParameters(Scattering);
    Resident->MaterialParametersEnabled = true;
    Resident->MaterialParameterCount = Scattering.Illumination.Configuration[0];
    Resident->MaterialParameterSelections = Scattering.TextureUse;
    Allocation = std::move(Resident);
}
SurfaceMaterialResidence::SurfaceMaterialResidence() = default;
SurfaceMaterialResidence::~SurfaceMaterialResidence() = default;
void SurfaceMaterialResidence::Finalize() noexcept
{
    Allocation.reset();
}
void SurfaceMaterialResidence::Initialize(const SurfaceRasterPrograms &Programs,const std::array<MaterialTexturePyramid,4> &Textures)
{
    const std::array<float,4> NeutralColour{1,1,1,0}, NeutralNormal{.5f,.5f,1,0};
    const std::array<float,4> NeutralRoughness{1,0,0,0}, NeutralMetalness{0,0,0,0}, NeutralOcclusion{1,0,0,0};
    const std::array<MaterialTexturePyramid,6> AuthoringMaps{
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::Albedo,NeutralColour),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::TangentNormal,NeutralNormal),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralRoughness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralMetalness),
        ConstructFloatMaterialPyramid(1,1,MaterialTextureCategory::OcclusionRoughnessMetallic,NeutralOcclusion)};
    Initialize(Programs,Textures,AuthoringMaps);
}
void SurfaceMaterialResidence::Initialize(const SurfaceRasterPrograms &Programs,
    const std::array<MaterialTexturePyramid,4> &Textures,const std::array<MaterialTexturePyramid,6> &AuthoringMaps)
{
    if (Allocation || !Programs.Allocation || !Programs.Allocation->MaterialSurface)
        throw std::runtime_error("Material textures require an active material program.");
    for (size_t Index = 0; Index < Textures.size(); ++Index)
        if (!ValidateMaterialPyramid(Textures[Index]) || uint32_t(Textures[Index].Category) != Index)
            throw std::runtime_error("Material textures require complete, correctly typed mip pyramids.");
    for (const auto &Map : AuthoringMaps)
        if (!ValidateMaterialPyramid(Map) || Map.FloatRgba.empty())
            throw std::runtime_error("Authoring maps require validated Float32 mip pyramids.");
    auto Resident = std::make_shared<SurfaceRasterization::RasterAllocation>();
    Resident->InitializeDispatch(Programs.Allocation->Access);
    Resident->SharedPrograms = Programs.Allocation;
    Resident->ConstructMaterialTextures(Textures,AuthoringMaps);
    Allocation = std::move(Resident);
}
uint64_t SurfaceMaterialResidence::QueryUploadCount() const noexcept
{
    uint64_t Uploads = 0;
    if (Allocation)
    {
        for (const auto &Image : Allocation->MaterialImages) Uploads += Image->TransferRecordings;
        for (const auto &Image : Allocation->AuthoringMapImages) Uploads += Image->TransferRecordings;
        if (Allocation->EnvironmentImage) Uploads += Allocation->EnvironmentImage->TransferRecordings;
    }
    return Uploads;
}
} // namespace Frontier

#undef FRONTIER_RASTER_FUNCTIONS
