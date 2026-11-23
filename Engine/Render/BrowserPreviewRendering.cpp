/*==========================================================================================================================================
                                                       BROWSERPREVIEWRENDERING.CPP
==========================================================================================================================================*/
// 🧩 One MSAA scene/contour rasterizer with a live canvas, bounded LRU tiles and decoded import images; caller owns submission.

#include "BrowserPreviewRendering.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stb_image.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Frontier
{
namespace
{
void RequireBrowserSuccess(VkResult Result)
{
    if (Result != VK_SUCCESS)
        throw std::runtime_error("Vulkan browser preview operation refused: " + std::to_string(int(Result)));
}

std::vector<uint32_t> DecodeBrowserShader(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4)
        throw std::runtime_error("Missing or malformed browser shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203)
        throw std::runtime_error("Invalid browser shader archive.");
    return Words;
}

constexpr uint32_t BrowserSampleCount = VK_SAMPLE_COUNT_4_BIT;
constexpr uint32_t BrowserTileWidth = 384, BrowserTileHeight = 246;
constexpr size_t BrowserTileSlots = 12, BrowserImportSlots = 4;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPreviewRendering::RenderingAllocation
{
    VulkanRasterAccess Access;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkPhysicalDeviceProperties Limits{};
    bool Finalized = false;

#define FRONTIER_BROWSER_FUNCTIONS(Function)                                                                          \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) Function(AllocateMemory)     \
        Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory)                     \
            Function(CreateImage) Function(DestroyImage) Function(GetImageMemoryRequirements)                         \
                Function(BindImageMemory) Function(CreateImageView) Function(DestroyImageView)                         \
                    Function(CreateRenderPass) Function(DestroyRenderPass) Function(CreateFramebuffer)                \
                        Function(DestroyFramebuffer) Function(CreatePipelineLayout) Function(DestroyPipelineLayout)    \
                            Function(CreateShaderModule) Function(DestroyShaderModule)                                \
                                Function(CreateGraphicsPipelines) Function(DestroyPipeline) Function(CmdCopyBuffer)   \
                                    Function(CmdPipelineBarrier) Function(CmdBeginRenderPass)                         \
                                        Function(CmdEndRenderPass) Function(CmdBindPipeline)                          \
                                            Function(CmdBindVertexBuffers) Function(CmdPushConstants)                \
                                                Function(CmdSetViewport) Function(CmdSetScissor) Function(CmdDraw)   \
                                                    Function(CreateDescriptorSetLayout)                              \
                                                        Function(DestroyDescriptorSetLayout)                         \
                                                            Function(CreateDescriptorPool)                           \
                                                                Function(DestroyDescriptorPool)                      \
                                                                    Function(AllocateDescriptorSets)                 \
                                                                        Function(UpdateDescriptorSets)               \
                                                                            Function(CreateSampler)                  \
                                                                                Function(DestroySampler)             \
                                                                                    Function(CmdCopyBufferToImage)   \
                                                                                        Function(CmdBindDescriptorSets)
#define FRONTIER_BROWSER_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_BROWSER_FUNCTIONS(FRONTIER_BROWSER_POINTER)
#undef FRONTIER_BROWSER_POINTER

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
    struct BrowserTarget
    {
        ImageFootprint Multisample, Resolve, Depth;
        VkFramebuffer Frame = VK_NULL_HANDLE;
        VkExtent2D Extent{};
    };
    struct BrowserGeometry
    {
        BufferFootprint Triangles, TriangleTransfer, Contours, ContourTransfer;
        uint32_t TriangleVertices = 0, ContourVertices = 0;
        bool TransferPending = true, TransferRecorded = false;
    };
    struct TileSlot
    {
        std::string Key;
        BrowserPreviewConfiguration Configuration;
        BrowserPreviewCamera Camera;
        int Channel = 0;
        uint64_t Generation = 0, Access = 0;
        bool Ready = false, Dirty = false, Captured = false;
        std::shared_ptr<BrowserTarget> Target;
    };
    struct ImportSlot
    {
        std::string Key;
        uint32_t Width = 0, Height = 0;
        uint64_t Generation = 0, Access = 0, UploadSerial = 0;
        bool Ready = false, UploadPending = false;
        std::string Diagnostic;
        ImageFootprint Image;
        BufferFootprint Transfer;
    };
    struct RetiredTarget
    {
        uint64_t Serial;
        std::shared_ptr<BrowserTarget> Target;
    };
    struct RetiredImport
    {
        uint64_t Serial;
        ImageFootprint Image;
        BufferFootprint Transfer;
    };

    VkDescriptorSetLayout EonSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Scene = VK_NULL_HANDLE, SceneOffset = VK_NULL_HANDLE, ContourHidden = VK_NULL_HANDLE,
               ContourVisible = VK_NULL_HANDLE;
    VkRenderPass Rasterization = VK_NULL_HANDLE;
    VkSampler EonSampler = VK_NULL_HANDLE;
    VkDescriptorPool EonPool = VK_NULL_HANDLE;
    VkDescriptorSet EonDescriptor = VK_NULL_HANDLE;
    ImageFootprint EonImage;
    BufferFootprint EonTransfer;
    bool EonPending = true, EonRecorded = false;
    std::map<std::string, std::shared_ptr<BrowserGeometry>> Geometries;
    std::shared_ptr<BrowserTarget> Live;
    BrowserPreviewConfiguration LiveConfiguration;
    BrowserPreviewCamera LiveCamera;
    int LiveChannel = 0;
    bool LiveDirty = false, LiveReady = false, LiveConfigured = false;
    std::vector<TileSlot> Tiles;
    std::vector<ImportSlot> Imports;
    std::vector<RetiredTarget> Retirements;
    std::vector<RetiredImport> ImportRetirements;
    uint64_t Clock = 0, GenerationClock = 0, SubmittedSerial = 0;
    std::string RecordedKey; // Empty records the live canvas; otherwise the tile key.
    bool RecordedLive = false;
    std::string Diagnostic;

    ~RenderingAllocation() { Finalize(); }

    void Finalize() noexcept
    {
        if (!Access.Device || Finalized) return;
        Finalized = true;
        for (auto &Retired : Retirements)
            if (Retired.Target) ReleaseTarget(*Retired.Target);
        Retirements.clear();
        for (auto &Retired : ImportRetirements)
        {
            ReleaseImage(Retired.Image);
            ReleaseBuffer(Retired.Transfer);
        }
        ImportRetirements.clear();
        if (Live) ReleaseTarget(*Live);
        Live.reset();
        for (auto &Slot : Tiles)
            if (Slot.Target) ReleaseTarget(*Slot.Target);
        Tiles.clear();
        for (auto &Pair : Geometries)
        {
            ReleaseBuffer(Pair.second->Triangles);
            ReleaseBuffer(Pair.second->TriangleTransfer);
            ReleaseBuffer(Pair.second->Contours);
            ReleaseBuffer(Pair.second->ContourTransfer);
        }
        for (auto &Import : Imports) ReleaseImport(Import);
        Imports.clear();
        Geometries.clear();
        if (EonPool) DestroyDescriptorPool(Access.Device, EonPool, nullptr);
        if (EonSampler) DestroySampler(Access.Device, EonSampler, nullptr);
        ReleaseImage(EonImage);
        ReleaseBuffer(EonTransfer);
        if (Scene) DestroyPipeline(Access.Device, Scene, nullptr);
        if (SceneOffset) DestroyPipeline(Access.Device, SceneOffset, nullptr);
        if (ContourHidden) DestroyPipeline(Access.Device, ContourHidden, nullptr);
        if (ContourVisible) DestroyPipeline(Access.Device, ContourVisible, nullptr);
        if (PipelineLayout) DestroyPipelineLayout(Access.Device, PipelineLayout, nullptr);
        if (EonSetLayout) DestroyDescriptorSetLayout(Access.Device, EonSetLayout, nullptr);
        if (Rasterization) DestroyRenderPass(Access.Device, Rasterization, nullptr);
        EonPool = VK_NULL_HANDLE;
        EonSampler = VK_NULL_HANDLE;
        Scene = SceneOffset = ContourHidden = ContourVisible = VK_NULL_HANDLE;
        PipelineLayout = VK_NULL_HANDLE;
        EonSetLayout = VK_NULL_HANDLE;
        Rasterization = VK_NULL_HANDLE;
        Access = {};
    }

    void ReleaseBuffer(BufferFootprint &Footprint) noexcept
    {
        if (!Access.Device) return;
        if (Footprint.Buffer) DestroyBuffer(Access.Device, Footprint.Buffer, nullptr);
        if (Footprint.Memory) FreeMemory(Access.Device, Footprint.Memory, nullptr);
        Footprint = {};
    }
    void ReleaseImage(ImageFootprint &Footprint) noexcept
    {
        if (!Access.Device) return;
        if (Footprint.View) DestroyImageView(Access.Device, Footprint.View, nullptr);
        if (Footprint.Image) DestroyImage(Access.Device, Footprint.Image, nullptr);
        if (Footprint.Memory) FreeMemory(Access.Device, Footprint.Memory, nullptr);
        Footprint = {};
    }
    void ReleaseTarget(BrowserTarget &Target) noexcept
    {
        if (!Access.Device) return;
        if (Target.Frame) DestroyFramebuffer(Access.Device, Target.Frame, nullptr);
        ReleaseImage(Target.Multisample);
        ReleaseImage(Target.Resolve);
        ReleaseImage(Target.Depth);
        Target.Frame = VK_NULL_HANDLE;
    }
    void ReleaseImport(ImportSlot &Import) noexcept
    {
        ReleaseImage(Import.Image);
        ReleaseBuffer(Import.Transfer);
    }

    uint32_t ResolveMemory(uint32_t Allowed, VkMemoryPropertyFlags Required)
    {
        for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
            if ((Allowed & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Required) == Required)
                return Index;
        throw std::runtime_error("Required Vulkan browser memory is unavailable.");
    }

    void ConstructBuffer(
        BufferFootprint &Footprint, VkDeviceSize Bytes, VkBufferUsageFlags Usage, VkMemoryPropertyFlags Properties)
    {
        Footprint.Bytes = Bytes;
        VkBufferCreateInfo Declaration{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Declaration.size = Bytes;
        Declaration.usage = Usage;
        Declaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        RequireBrowserSuccess(CreateBuffer(Access.Device, &Declaration, nullptr, &Footprint.Buffer));
        VkMemoryRequirements Requirements;
        GetBufferMemoryRequirements(Access.Device, Footprint.Buffer, &Requirements);
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = ResolveMemory(Requirements.memoryTypeBits, Properties);
        RequireBrowserSuccess(AllocateMemory(Access.Device, &Allocation, nullptr, &Footprint.Memory));
        RequireBrowserSuccess(BindBufferMemory(Access.Device, Footprint.Buffer, Footprint.Memory, 0));
    }

    void ConstructImage(ImageFootprint &Footprint, uint32_t Width, uint32_t Height, VkFormat Format,
        VkImageUsageFlags Usage, VkImageAspectFlags Aspect, uint32_t Samples)
    {
        VkImageCreateInfo Declaration{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        Declaration.imageType = VK_IMAGE_TYPE_2D;
        Declaration.format = Format;
        Declaration.extent = {Width, Height, 1};
        Declaration.mipLevels = 1;
        Declaration.arrayLayers = 1;
        Declaration.samples = VkSampleCountFlagBits(Samples);
        Declaration.tiling = VK_IMAGE_TILING_OPTIMAL;
        Declaration.usage = Usage;
        Declaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Declaration.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        RequireBrowserSuccess(CreateImage(Access.Device, &Declaration, nullptr, &Footprint.Image));
        VkMemoryRequirements Requirements;
        GetImageMemoryRequirements(Access.Device, Footprint.Image, &Requirements);
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = ResolveMemory(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RequireBrowserSuccess(AllocateMemory(Access.Device, &Allocation, nullptr, &Footprint.Memory));
        RequireBrowserSuccess(BindImageMemory(Access.Device, Footprint.Image, Footprint.Memory, 0));
        VkImageViewCreateInfo View{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        View.image = Footprint.Image;
        View.viewType = VK_IMAGE_VIEW_TYPE_2D;
        View.format = Format;
        View.subresourceRange = {Aspect, 0, 1, 0, 1};
        RequireBrowserSuccess(CreateImageView(Access.Device, &View, nullptr, &Footprint.View));
    }

    VkShaderModule ConstructShader(const std::vector<uint32_t> &Words)
    {
        VkShaderModuleCreateInfo Declaration{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        Declaration.codeSize = Words.size() * 4;
        Declaration.pCode = Words.data();
        VkShaderModule Program = VK_NULL_HANDLE;
        RequireBrowserSuccess(CreateShaderModule(Access.Device, &Declaration, nullptr, &Program));
        return Program;
    }

    void ConstructPipeline(VkPipeline &Program, const std::vector<uint32_t> &Vertex,
        const std::vector<uint32_t> &Fragment, bool Contours, bool DepthBias, VkCompareOp DepthCompare,
        bool DepthWrite)
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
            Programs[0].pName = Programs[1].pName = "main";
            VkVertexInputBindingDescription Input{
                0, Contours ? 52u : 40u, VK_VERTEX_INPUT_RATE_VERTEX};
            VkVertexInputAttributeDescription Attributes[5]{};
            if (Contours)
            {
                Attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
                Attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
                Attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24};
                Attributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36};
                Attributes[4] = {4, 0, VK_FORMAT_R32_SFLOAT, 48};
            }
            else
            {
                Attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
                Attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
                Attributes[2] = {2, 0, VK_FORMAT_R32_SFLOAT, 24};
                Attributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 28};
            }
            VkPipelineVertexInputStateCreateInfo VertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VertexInput.vertexBindingDescriptionCount = 1;
            VertexInput.pVertexBindingDescriptions = &Input;
            VertexInput.vertexAttributeDescriptionCount = Contours ? 5 : 4;
            VertexInput.pVertexAttributeDescriptions = Attributes;
            VkPipelineInputAssemblyStateCreateInfo Assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            Assembly.topology =
                Contours ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo Viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            Viewport.viewportCount = Viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo Raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            Raster.polygonMode = VK_POLYGON_MODE_FILL;
            Raster.cullMode = VK_CULL_MODE_NONE;
            Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            Raster.lineWidth = 1;
            Raster.depthBiasEnable = DepthBias ? VK_TRUE : VK_FALSE;
            Raster.depthBiasConstantFactor = 1;
            Raster.depthBiasSlopeFactor = 1;
            VkPipelineMultisampleStateCreateInfo Samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            Samples.rasterizationSamples = VkSampleCountFlagBits(BrowserSampleCount);
            VkPipelineDepthStencilStateCreateInfo DepthTest{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            DepthTest.depthTestEnable = VK_TRUE;
            DepthTest.depthWriteEnable = DepthWrite ? VK_TRUE : VK_FALSE;
            DepthTest.depthCompareOp = DepthCompare;
            VkPipelineColorBlendAttachmentState Blend{};
            Blend.colorWriteMask = 15;
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
            Declaration.layout = PipelineLayout;
            Declaration.renderPass = Rasterization;
            RequireBrowserSuccess(
                CreateGraphicsPipelines(Access.Device, VK_NULL_HANDLE, 1, &Declaration, nullptr, &Program));
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
        VkAttachmentDescription Attachments[3]{};
        Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        Attachments[0].samples = VkSampleCountFlagBits(BrowserSampleCount);
        Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        Attachments[1].format = VK_FORMAT_D32_SFLOAT;
        Attachments[1].samples = VkSampleCountFlagBits(BrowserSampleCount);
        Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        Attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        Attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        Attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        Attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ColourTarget{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference DepthTarget{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference ResolveTarget{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription Subdivision{};
        Subdivision.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subdivision.colorAttachmentCount = 1;
        Subdivision.pColorAttachments = &ColourTarget;
        Subdivision.pResolveAttachments = &ResolveTarget;
        Subdivision.pDepthStencilAttachment = &DepthTarget;
        VkSubpassDependency Dependencies[2]{};
        Dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        Dependencies[0].dstSubpass = 0;
        Dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        Dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        Dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        Dependencies[1].srcSubpass = 0;
        Dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        Dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo Declaration{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        Declaration.attachmentCount = 3;
        Declaration.pAttachments = Attachments;
        Declaration.subpassCount = 1;
        Declaration.pSubpasses = &Subdivision;
        Declaration.dependencyCount = 2;
        Declaration.pDependencies = Dependencies;
        RequireBrowserSuccess(CreateRenderPass(Access.Device, &Declaration, nullptr, &Rasterization));
    }

    std::shared_ptr<BrowserTarget> ConstructTarget(uint32_t Width, uint32_t Height)
    {
        if (!Width || !Height || Width > 1600 || Height > 1200 || Width > Limits.limits.maxImageDimension2D ||
            Height > Limits.limits.maxImageDimension2D || Width > Limits.limits.maxFramebufferWidth ||
            Height > Limits.limits.maxFramebufferHeight)
            throw std::runtime_error("Browser preview extent is outside the 1600x1200 reference clamp.");
        auto Target = std::make_shared<BrowserTarget>();
        try
        {
            ConstructImage(Target->Multisample, Width, Height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, BrowserSampleCount);
            ConstructImage(Target->Resolve, Width, Height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, 1);
            ConstructImage(Target->Depth, Width, Height, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, BrowserSampleCount);
            VkImageView Views[] = {Target->Multisample.View, Target->Depth.View, Target->Resolve.View};
            VkFramebufferCreateInfo Declaration{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            Declaration.renderPass = Rasterization;
            Declaration.attachmentCount = 3;
            Declaration.pAttachments = Views;
            Declaration.width = Width;
            Declaration.height = Height;
            Declaration.layers = 1;
            RequireBrowserSuccess(CreateFramebuffer(Access.Device, &Declaration, nullptr, &Target->Frame));
        }
        catch (...)
        {
            ReleaseTarget(*Target);
            throw;
        }
        Target->Extent = {Width, Height};
        return Target;
    }

    void ConstructEon()
    {
        // 📝 Fixed linear-clamp sampler; one immutable 64x32 lookup serves every target.
        VkDescriptorSetLayoutBinding Bindings[2]{};
        Bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        Bindings[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo Declaration{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        Declaration.bindingCount = 2;
        Declaration.pBindings = Bindings;
        RequireBrowserSuccess(CreateDescriptorSetLayout(Access.Device, &Declaration, nullptr, &EonSetLayout));
        VkSamplerCreateInfo Sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        Sampler.magFilter = Sampler.minFilter = VK_FILTER_LINEAR;
        Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        Sampler.addressModeU = Sampler.addressModeV = Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Sampler.maxAnisotropy = 1;
        RequireBrowserSuccess(CreateSampler(Access.Device, &Sampler, nullptr, &EonSampler));
        const std::vector<uint8_t> Lookup = GenerateBrowserEonLookup();
        ConstructImage(EonImage, 64, 32, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        ConstructBuffer(EonTransfer, Lookup.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireBrowserSuccess(MapMemory(Access.Device, EonTransfer.Memory, 0, Lookup.size(), 0, &Mapped));
        std::memcpy(Mapped, Lookup.data(), Lookup.size());
        UnmapMemory(Access.Device, EonTransfer.Memory);
        const VkDescriptorPoolSize Sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        Pool.maxSets = 1;
        Pool.poolSizeCount = 2;
        Pool.pPoolSizes = Sizes;
        RequireBrowserSuccess(CreateDescriptorPool(Access.Device, &Pool, nullptr, &EonPool));
        VkDescriptorSetAllocateInfo Allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocation.descriptorPool = EonPool;
        Allocation.descriptorSetCount = 1;
        Allocation.pSetLayouts = &EonSetLayout;
        RequireBrowserSuccess(AllocateDescriptorSets(Access.Device, &Allocation, &EonDescriptor));
        VkDescriptorImageInfo Images[2]{};
        Images[0].imageView = EonImage.View;
        Images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Images[1].sampler = EonSampler;
        VkWriteDescriptorSet Writes[2]{};
        for (uint32_t Index = 0; Index < 2; ++Index)
        {
            Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[Index].dstSet = EonDescriptor;
            Writes[Index].dstBinding = Index;
            Writes[Index].descriptorCount = 1;
            Writes[Index].descriptorType = Sizes[Index].type;
            Writes[Index].pImageInfo = &Images[Index];
        }
        UpdateDescriptorSets(Access.Device, 2, Writes, 0, nullptr);
    }

    std::shared_ptr<BrowserGeometry> ResidenceFor(const BrowserPreviewConfiguration &Configuration)
    {
        const int Category = int(Configuration.Category);
        if (Category < 0 || Category > 2 || Configuration.Revision > 2 || Configuration.View > 16)
            throw std::runtime_error("Browser preview configuration is outside the reference catalogue.");
        const std::string Key =
            std::to_string(Category) + ":" + std::to_string(Configuration.Revision);
        const auto Cached = Geometries.find(Key);
        if (Cached != Geometries.end()) return Cached->second;
        static const std::vector<float> Actuator = GenerateBrowserActuatorTriangles();
        const std::vector<float> Triangles =
            GenerateBrowserPreviewTriangles(Actuator, Category, Configuration.Revision);
        const std::vector<float> Contours = GenerateBrowserDraftingContours(Triangles);
        auto Geometry = std::make_shared<BrowserGeometry>();
        Geometry->TriangleVertices = uint32_t(Triangles.size() / 10);
        Geometry->ContourVertices = uint32_t(Contours.size() / 13);
        const auto Upload = [&](BufferFootprint &Resident, BufferFootprint &Transfer, const void *Source, size_t Bytes)
        {
            ConstructBuffer(Resident, Bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            ConstructBuffer(Transfer, Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            void *Mapped = nullptr;
            RequireBrowserSuccess(MapMemory(Access.Device, Transfer.Memory, 0, Bytes, 0, &Mapped));
            std::memcpy(Mapped, Source, Bytes);
            UnmapMemory(Access.Device, Transfer.Memory);
        };
        try
        {
            // 📝 Vulkan callers reject zero-byte buffers, so empty contour sets still allocate one vertex.
            Upload(Geometry->Triangles, Geometry->TriangleTransfer, Triangles.data(),
                std::max<size_t>(Triangles.size() * 4, 40));
            static const std::array<float, 13> EmptyContour{};
            Upload(Geometry->Contours, Geometry->ContourTransfer,
                Contours.empty() ? EmptyContour.data() : Contours.data(), std::max<size_t>(Contours.size() * 4, 52));
        }
        catch (...)
        {
            ReleaseBuffer(Geometry->Triangles);
            ReleaseBuffer(Geometry->TriangleTransfer);
            ReleaseBuffer(Geometry->Contours);
            ReleaseBuffer(Geometry->ContourTransfer);
            throw;
        }
        Geometries.emplace(Key, Geometry);
        DiagnosticJournal::Append("BrowserGeometry",
            "resident=" + Key + " triangles=" + std::to_string(Geometry->TriangleVertices / 3) +
                " contour_vertices=" + std::to_string(Geometry->ContourVertices));
        return Geometry;
    }

    void RecordImageTransfer(VkCommandBuffer Commands, const BufferFootprint &Transfer, ImageFootprint &Image,
        uint32_t Width, uint32_t Height)
    {
        VkImageMemoryBarrier Barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        Barrier.srcAccessMask = 0;
        Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.srcQueueFamilyIndex = Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image = Image.Image;
        Barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &Barrier);
        VkBufferImageCopy Region{};
        Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        Region.imageExtent = {Width, Height, 1};
        CmdCopyBufferToImage(Commands, Transfer.Buffer, Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &Barrier);
    }

    BrowserPreviewProjection MakeProjection(const BrowserPreviewConfiguration &Configuration,
        const BrowserPreviewCamera &Camera, int Channel, float Ink, uint32_t Width, uint32_t Height)
    {
        if (!std::isfinite(Camera.Azimuth) || !std::isfinite(Camera.Elevation) || !std::isfinite(Camera.Distance) ||
            Camera.Distance < 4 || Camera.Distance > 28 || Channel < 0 || Channel > 5)
            throw std::runtime_error("Browser preview camera or channel is outside the reference contract.");
        for (const double Axis : Camera.Target)
            if (!std::isfinite(Axis) || std::abs(Axis) > 100)
                throw std::runtime_error("Browser preview target is outside the reference contract.");
        std::array<float, 3> Eye{};
        std::array<float, 16> Product = ConstructBrowserViewProjection(
            Camera, double(Width) / double(Height), Channel == 5, Eye);
        // 📝 GL clip remap: negate the Y row (Vulkan 1.0 core has no negative viewport) and fold the depth row
        // to [0,1]. The fold must combine rows (row 2 with row 3); folding a column corrupts W and parks every
        // orthographic vertex at negative depth, which clipped all CAD contours to black.
        for (size_t Column = 0; Column < 4; ++Column) Product[Column * 4 + 1] = -Product[Column * 4 + 1];
        for (size_t Column = 0; Column < 4; ++Column)
            Product[Column * 4 + 2] = 0.5f * Product[Column * 4 + 2] + 0.5f * Product[Column * 4 + 3];
        for (const float Coefficient : Product)
            if (!std::isfinite(Coefficient)) throw std::runtime_error("Nonfinite browser projection refused.");
        std::array<double, 3> Direction{Eye[0] - Camera.Target[0], Eye[1] - Camera.Target[1], Eye[2] - Camera.Target[2]};
        const double Length = std::hypot(Direction[0], Direction[1], Direction[2]);
        for (double &Component : Direction) Component /= Length;
        BrowserPreviewProjection Projection;
        Projection.ClipFromSurface = Product;
        Projection.EyeChannel = {Eye[0], Eye[1], Eye[2], float(Channel)};
        Projection.RasterOptions = {0, Channel == 5 ? 0.f : 1.f, Ink, 0};
        Projection.ViewDirection = {float(Direction[0]), float(Direction[1]), float(Direction[2]), 0};
        return Projection;
    }

    void RecordTarget(VkCommandBuffer Commands, BrowserTarget &Target, const BrowserPreviewConfiguration &Configuration,
        const BrowserPreviewCamera &Camera, int Channel)
    {
        auto Geometry = ResidenceFor(Configuration);
        if (EonPending)
        {
            RecordImageTransfer(Commands, EonTransfer, EonImage, 64, 32);
            EonRecorded = true;
        }
        if (Geometry->TransferPending)
        {
            for (const auto &Pair : {std::pair{&Geometry->Triangles, &Geometry->TriangleTransfer},
                     std::pair{&Geometry->Contours, &Geometry->ContourTransfer}})
            {
                VkBufferCopy Region{0, 0, Pair.first->Bytes};
                CmdCopyBuffer(Commands, Pair.second->Buffer, Pair.first->Buffer, 1, &Region);
            }
            VkMemoryBarrier Barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1,
                &Barrier, 0, nullptr, 0, nullptr);
            Geometry->TransferRecorded = true;
        }
        // Validate + compute the projection BEFORE opening the render pass: MakeProjection throws outside the
        // reference contract, and a throw inside the pass would submit a half-recorded buffer (device lost).
        const bool Drafting = Channel == 5;
        auto Projection = MakeProjection(Configuration, Camera, Channel, .85f, Target.Extent.width, Target.Extent.height);
        VkClearValue Clear[3]{};
        Clear[0].color = {{0, 0, 0, 1}};
        Clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo Declaration{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        Declaration.renderPass = Rasterization;
        Declaration.framebuffer = Target.Frame;
        Declaration.renderArea.extent = Target.Extent;
        Declaration.clearValueCount = 2; // 📝 Resolve output is fully rendered; no load is required.
        Declaration.pClearValues = Clear;
        CmdBeginRenderPass(Commands, &Declaration, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport Viewport{0, 0, float(Target.Extent.width), float(Target.Extent.height), 0, 1};
        VkRect2D Scissor{{0, 0}, Target.Extent};
        CmdSetViewport(Commands, 0, 1, &Viewport);
        CmdSetScissor(Commands, 0, 1, &Scissor);
        CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof(Projection), &Projection);
        CmdBindDescriptorSets(
            Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &EonDescriptor, 0, nullptr);
        constexpr VkDeviceSize Offset = 0;
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Drafting ? SceneOffset : Scene);
        CmdBindVertexBuffers(Commands, 0, 1, &Geometry->Triangles.Buffer, &Offset);
        if (Geometry->TriangleVertices) CmdDraw(Commands, Geometry->TriangleVertices, 1, 0, 0);
        if (Drafting && Geometry->ContourVertices)
        {
            CmdBindVertexBuffers(Commands, 0, 1, &Geometry->Contours.Buffer, &Offset);
            if (Configuration.HiddenEdges)
            {
                auto Hidden = MakeProjection(
                    Configuration, Camera, Channel, .2f, Target.Extent.width, Target.Extent.height);
                CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(Hidden), &Hidden);
                CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ContourHidden);
                CmdDraw(Commands, Geometry->ContourVertices, 1, 0, 0);
                CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(Projection), &Projection);
            }
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ContourVisible);
            CmdDraw(Commands, Geometry->ContourVertices, 1, 0, 0);
        }
        CmdEndRenderPass(Commands);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

BrowserPreviewRendering::BrowserPreviewRendering() = default;
BrowserPreviewRendering::~BrowserPreviewRendering() = default;

void BrowserPreviewRendering::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory)
{
    if (Allocation) throw std::runtime_error("Browser preview rendering already initialized.");
    if (!Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryLimits)
        throw std::runtime_error("Browser preview rendering requires borrowed device access.");
    static_assert(sizeof(BrowserPreviewProjection) == 112, "Browser push constants must match BrowserProjection.slang.");
    DiagnosticJournal::Append(
        "BrowserGPU", "Initialize shader directory: " + DiagnosticJournal::EncodePath(ShaderDirectory));
    auto Resident = std::make_unique<RenderingAllocation>();
#define FRONTIER_LOAD_BROWSER(Name)                                                                                   \
    Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name));                \
    if (!Resident->Name) throw std::runtime_error("Vulkan browser entry point unavailable: vk" #Name);
    FRONTIER_BROWSER_FUNCTIONS(FRONTIER_LOAD_BROWSER)
#undef FRONTIER_LOAD_BROWSER
    Resident->Access = Access;
    Access.QueryMemory(Access.PhysicalDevice, &Resident->Memory);
    Access.QueryLimits(Access.PhysicalDevice, &Resident->Limits);
    if (Resident->Limits.limits.maxPushConstantsSize < sizeof(BrowserPreviewProjection))
        throw std::runtime_error("Browser preview push constants exceed device capacity.");
    if (!(Resident->Limits.limits.framebufferColorSampleCounts & BrowserSampleCount) ||
        !(Resident->Limits.limits.framebufferDepthSampleCounts & BrowserSampleCount))
        throw std::runtime_error("Browser previews require 4-sample colour and depth rasterization.");
    VkFormatProperties Properties;
    Access.QueryFormat(Access.PhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &Properties);
    constexpr auto Required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((Properties.optimalTilingFeatures & Required) != Required)
        throw std::runtime_error("Browser preview colour format features unavailable.");
    const std::array<const char *, 4> Names{
        "BrowserPreviewVertex", "BrowserPreviewFragment", "BrowserDraftingVertex", "BrowserDraftingFragment"};
    std::array<std::vector<uint32_t>, 4> Words;
    for (size_t Index = 0; Index < Names.size(); ++Index)
    {
        Words[Index] = DecodeBrowserShader(ShaderDirectory / (std::string(Names[Index]) + ".spv"));
        DiagnosticJournal::Append(
            "Shader", std::string(Names[Index]) + " bytes=" + std::to_string(Words[Index].size() * 4));
    }
    try
    {
        Resident->ConstructRasterization();
        Resident->ConstructEon();
        VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof(BrowserPreviewProjection)};
        VkPipelineLayoutCreateInfo LayoutDeclaration{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        LayoutDeclaration.setLayoutCount = 1;
        LayoutDeclaration.pSetLayouts = &Resident->EonSetLayout;
        LayoutDeclaration.pushConstantRangeCount = 1;
        LayoutDeclaration.pPushConstantRanges = &Range;
        RequireBrowserSuccess(
            Resident->CreatePipelineLayout(Access.Device, &LayoutDeclaration, nullptr, &Resident->PipelineLayout));
        Resident->ConstructPipeline(
            Resident->Scene, Words[0], Words[1], false, false, VK_COMPARE_OP_LESS, true);
        Resident->ConstructPipeline(
            Resident->SceneOffset, Words[0], Words[1], false, true, VK_COMPARE_OP_LESS, true);
        Resident->ConstructPipeline(
            Resident->ContourHidden, Words[2], Words[3], true, false, VK_COMPARE_OP_GREATER, false);
        Resident->ConstructPipeline(
            Resident->ContourVisible, Words[2], Words[3], true, false, VK_COMPARE_OP_LESS_OR_EQUAL, false);
        Resident->Tiles.resize(BrowserTileSlots);
        Resident->Imports.resize(BrowserImportSlots);
    }
    catch (...)
    {
        Resident->Finalize();
        throw;
    }
    Allocation = std::move(Resident);
}

void BrowserPreviewRendering::Finalize() noexcept
{
    Allocation.reset();
}

namespace
{
bool SameBrowserCamera(const BrowserPreviewCamera &Left, const BrowserPreviewCamera &Right)
{
    return Left.Azimuth == Right.Azimuth && Left.Elevation == Right.Elevation && Left.Distance == Right.Distance &&
           Left.Target == Right.Target;
}
bool SameBrowserConfiguration(const BrowserPreviewConfiguration &Left, const BrowserPreviewConfiguration &Right)
{
    return Left.Category == Right.Category && Left.View == Right.View && Left.Revision == Right.Revision &&
           Left.Sketch == Right.Sketch && Left.HiddenEdges == Right.HiddenEdges;
}
} // namespace

void BrowserPreviewRendering::ConfigureLive(const BrowserPreviewConfiguration &Configuration,
    const BrowserPreviewCamera &Camera, int Channel, int Width, int Height)
{
    if (!Allocation) throw std::runtime_error("Browser preview rendering is not initialized.");
    auto &Rendering = *Allocation;
    // 📝 Reference Rasterize clamps to 1600x1200; nonpositive extents keep the previous canvas.
    const uint32_t ClampedWidth = uint32_t(std::max(1, std::min(1600, Width)));
    const uint32_t ClampedHeight = uint32_t(std::max(1, std::min(1200, Height)));
    if (!Rendering.LiveConfigured || !Rendering.Live || Rendering.Live->Extent.width != ClampedWidth ||
        Rendering.Live->Extent.height != ClampedHeight)
    {
        if (Rendering.Live) Rendering.Retirements.push_back({Rendering.SubmittedSerial, std::move(Rendering.Live)});
        Rendering.Live = Rendering.ConstructTarget(ClampedWidth, ClampedHeight);
        Rendering.LiveConfigured = true;
        Rendering.LiveDirty = true;
        Rendering.LiveReady = false;
    }
    // Clamp the stored camera to the reference contract (self-heals any drifted input); compare clamped copies
    // so a clamped input never reports dirty forever.
    BrowserPreviewCamera Clamped = Camera;
    if (!std::isfinite(Clamped.Azimuth) || !std::isfinite(Clamped.Elevation) || !std::isfinite(Clamped.Distance))
        Clamped = ConfigureBrowserCamera(int(Configuration.Category), Configuration.View, Configuration.Sketch);
    Clamped.Distance = std::clamp(Clamped.Distance, 4.0, 28.0);
    for (double &Axis : Clamped.Target)
        Axis = std::isfinite(Axis) ? std::clamp(Axis, -100.0, 100.0) : 0;
    if (!Rendering.LiveDirty &&
        (!SameBrowserConfiguration(Rendering.LiveConfiguration, Configuration) ||
            !SameBrowserCamera(Rendering.LiveCamera, Clamped) || Rendering.LiveChannel != Channel))
    {
        Rendering.LiveDirty = true;
        Rendering.LiveReady = false;
    }
    Rendering.LiveConfiguration = Configuration;
    Rendering.LiveCamera = Clamped;
    Rendering.LiveChannel = Channel;
}

void BrowserPreviewRendering::RequestThumbnail(const std::string &Key, const BrowserPreviewConfiguration &Configuration)
{
    if (!Allocation) throw std::runtime_error("Browser preview rendering is not initialized.");
    if (Key.empty() || Key.size() > 128) throw std::runtime_error("Browser thumbnail key is invalid.");
    auto &Rendering = *Allocation;
    ++Rendering.Clock;
    auto Slot = std::find_if(Rendering.Tiles.begin(), Rendering.Tiles.end(),
        [&](const auto &Entry) { return Entry.Key == Key; });
    if (Slot == Rendering.Tiles.end())
    {
        Slot = std::min_element(Rendering.Tiles.begin(), Rendering.Tiles.end(),
            [](const auto &Left, const auto &Right) { return Left.Access < Right.Access; });
        if (!Slot->Key.empty() && Slot->Target) Rendering.Retirements.push_back({Rendering.SubmittedSerial, Slot->Target});
        *Slot = {};
        Slot->Key = Key;
        Slot->Generation = ++Rendering.GenerationClock;
        Slot->Target = Rendering.ConstructTarget(BrowserTileWidth, BrowserTileHeight);
        Slot->Camera = ConfigureBrowserCamera(
            int(Configuration.Category), Configuration.View, Configuration.Sketch);
    }
    Slot->Access = Rendering.Clock;
    // 📝 Captures own the camera/channel afterwards; only a new subject returns the slot to its default framing.
    if (!SameBrowserConfiguration(Slot->Configuration, Configuration))
    {
        Slot->Configuration = Configuration;
        Slot->Channel = Configuration.Channel;
        Slot->Camera = ConfigureBrowserCamera(int(Configuration.Category), Configuration.View, Configuration.Sketch);
        Slot->Dirty = true;
        Slot->Ready = false;
    }
    else if (!Slot->Ready) Slot->Dirty = true;
}

void BrowserPreviewRendering::CaptureThumbnail(const std::string &Key, const BrowserPreviewConfiguration &Configuration,
    const BrowserPreviewCamera &Camera, int Channel)
{
    RequestThumbnail(Key, Configuration);
    auto &Rendering = *Allocation;
    auto Slot = std::find_if(Rendering.Tiles.begin(), Rendering.Tiles.end(),
        [&](const auto &Entry) { return Entry.Key == Key; });
    if (Slot == Rendering.Tiles.end()) throw std::runtime_error("Browser thumbnail slot vanished.");
    Slot->Configuration = Configuration;
    Slot->Camera = Camera; // 📝 HTML Capture copies the live canvas pixels, including its camera.
    Slot->Channel = Channel;
    Slot->Dirty = true;
    Slot->Ready = false;
    Slot->Captured = true;
}

void BrowserPreviewRendering::RequestImportImage(const std::string &Key, const std::vector<uint8_t> &ImageBytes)
{
    if (!Allocation) throw std::runtime_error("Browser preview rendering is not initialized.");
    if (Key.empty() || Key.size() > 128) throw std::runtime_error("Browser import key is invalid.");
    auto &Rendering = *Allocation;
    ++Rendering.Clock;
    auto Slot = std::find_if(Rendering.Imports.begin(), Rendering.Imports.end(),
        [&](const auto &Entry) { return Entry.Key == Key; });
    if (Slot != Rendering.Imports.end())
    {
        Slot->Access = Rendering.Clock;
        return; // 📝 Imports are immutable; the first decode wins and failures persist for the session.
    }
    Slot = std::min_element(Rendering.Imports.begin(), Rendering.Imports.end(),
        [](const auto &Left, const auto &Right) { return Left.Access < Right.Access; });
    if (Slot->Image.Image || Slot->Transfer.Buffer)
        Rendering.ImportRetirements.push_back({Rendering.SubmittedSerial, Slot->Image, Slot->Transfer});
    *Slot = {};
    Slot->Key = Key;
    Slot->Access = Rendering.Clock;
    Slot->Generation = ++Rendering.GenerationClock;
    if (ImageBytes.empty() || ImageBytes.size() > 16777216)
    {
        Slot->Diagnostic = "Import image bytes are missing or exceed 16 MiB.";
        return;
    }
    int Width = 0, Height = 0, Channels = 0;
    if (!stbi_info_from_memory(ImageBytes.data(), int(ImageBytes.size()), &Width, &Height, &Channels) || Width <= 0 ||
        Height <= 0 || Width > 2048 || Height > 2048 || uint64_t(Width) * uint64_t(Height) > 4194304)
    {
        Slot->Diagnostic = "Import image is undecodable or exceeds 2048x2048.";
        return;
    }
    int DecodedChannels = 0;
    stbi_uc *Pixels =
        stbi_load_from_memory(ImageBytes.data(), int(ImageBytes.size()), &Width, &Height, &DecodedChannels, 4);
    if (!Pixels)
    {
        Slot->Diagnostic = std::string("Import image decode refused: ") + stbi_failure_reason();
        return;
    }
    try
    {
        Rendering.ConstructImage(Slot->Image, uint32_t(Width), uint32_t(Height), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        const size_t Bytes = size_t(Width) * size_t(Height) * 4;
        Rendering.ConstructBuffer(Slot->Transfer, Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void *Mapped = nullptr;
        RequireBrowserSuccess(
            Rendering.MapMemory(Rendering.Access.Device, Slot->Transfer.Memory, 0, Bytes, 0, &Mapped));
        std::memcpy(Mapped, Pixels, Bytes);
        Rendering.UnmapMemory(Rendering.Access.Device, Slot->Transfer.Memory);
    }
    catch (const std::exception &Failure)
    {
        Rendering.ReleaseImport(*Slot);
        Slot->Key = Key;
        Slot->Diagnostic = Failure.what();
        stbi_image_free(Pixels);
        return;
    }
    stbi_image_free(Pixels);
    Slot->Width = uint32_t(Width);
    Slot->Height = uint32_t(Height);
    Slot->UploadPending = true;
}

void BrowserPreviewRendering::ConstructCommands(
    VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation) throw std::runtime_error("Browser preview rendering is not initialized.");
    if (!Commands) throw std::runtime_error("Browser preview command buffer is unavailable.");
    auto &Rendering = *Allocation;
    for (auto Retired = Rendering.Retirements.begin(); Retired != Rendering.Retirements.end();)
    {
        if (Retired->Serial > CompletedSerial)
        {
            ++Retired;
            continue;
        }
        if (Retired->Target) Rendering.ReleaseTarget(*Retired->Target);
        Retired = Rendering.Retirements.erase(Retired);
    }
    for (auto Retired = Rendering.ImportRetirements.begin(); Retired != Rendering.ImportRetirements.end();)
    {
        if (Retired->Serial > CompletedSerial)
        {
            ++Retired;
            continue;
        }
        Rendering.ReleaseImage(Retired->Image);
        Rendering.ReleaseBuffer(Retired->Transfer);
        Retired = Rendering.ImportRetirements.erase(Retired);
    }
    for (auto &Import : Rendering.Imports)
        if (!Import.Transfer.Buffer || (Import.UploadSerial && Import.UploadSerial <= CompletedSerial))
            Rendering.ReleaseBuffer(Import.Transfer); // 📝 Staging is freed once its upload completes.
    Rendering.SubmittedSerial = SubmissionSerial;
    Rendering.RecordedLive = false;
    Rendering.RecordedKey.clear();
    if (Rendering.Live && Rendering.LiveDirty)
    {
        Rendering.RecordTarget(
            Commands, *Rendering.Live, Rendering.LiveConfiguration, Rendering.LiveCamera, Rendering.LiveChannel);
        Rendering.RecordedLive = true;
        return; // 📝 One target per submission keeps first-use transfers single-recorded.
    }
    const auto Dirty = std::min_element(Rendering.Tiles.begin(), Rendering.Tiles.end(),
        [](const auto &Left, const auto &Right)
        {
            // Explicit captures jump the queue; older dirty slots keep LRU order behind them.
            if (Left.Captured != Right.Captured) return Left.Captured;
            if (Left.Dirty != Right.Dirty) return Left.Dirty;
            return Left.Access < Right.Access;
        });
    if (Dirty != Rendering.Tiles.end() && Dirty->Dirty && Dirty->Target)
    {
        static int TracedSubmissions = 0;
        const bool Trace = TracedSubmissions < 3;
        if (Trace) DiagnosticJournal::Append("BrowserRecord", "serial=" + std::to_string(SubmissionSerial) + " key=" + Dirty->Key);
        Rendering.RecordTarget(Commands, *Dirty->Target, Dirty->Configuration, Dirty->Camera, Dirty->Channel);
        if (Trace)
        {
            DiagnosticJournal::Append("BrowserRecorded", "serial=" + std::to_string(SubmissionSerial) + " key=" + Dirty->Key);
            ++TracedSubmissions;
        }
        Rendering.RecordedKey = Dirty->Key;
        return;
    }
    for (auto &Import : Rendering.Imports)
    {
        if (!Import.UploadPending || !Import.Image.Image) continue;
        Rendering.RecordImageTransfer(Commands, Import.Transfer, Import.Image, Import.Width, Import.Height);
        Import.UploadPending = false;
        Import.UploadSerial = SubmissionSerial;
    }
}

void BrowserPreviewRendering::ConfirmSubmission() noexcept
{
    if (!Allocation) return;
    auto &Rendering = *Allocation;
    if (Rendering.EonRecorded)
    {
        Rendering.EonPending = false;
        Rendering.EonRecorded = false;
    }
    for (auto &Pair : Rendering.Geometries)
    {
        if (Pair.second->TransferPending && Pair.second->TransferRecorded)
        {
            Pair.second->TransferPending = false;
            Pair.second->TransferRecorded = false;
        }
    }
    if (Rendering.RecordedLive && Rendering.Live)
    {
        Rendering.LiveReady = true;
        Rendering.LiveDirty = false;
    }
    if (!Rendering.RecordedKey.empty())
    {
        static int TracedConfirmations = 0;
        if (TracedConfirmations < 3)
        {
            DiagnosticJournal::Append("BrowserConfirm", "key=" + Rendering.RecordedKey);
            ++TracedConfirmations;
        }
        const auto Slot = std::find_if(Rendering.Tiles.begin(), Rendering.Tiles.end(),
            [&](const auto &Entry) { return Entry.Key == Rendering.RecordedKey; });
        if (Slot != Rendering.Tiles.end())
        {
            Slot->Ready = true;
            Slot->Dirty = false;
            Slot->Captured = false;
        }
    }
    for (auto &Import : Rendering.Imports)
        if (!Import.UploadPending && Import.Image.Image && !Import.Ready && Import.UploadSerial) Import.Ready = true;
}

bool BrowserPreviewRendering::AuthenticateThumbnail(const std::string &Key, uint64_t Generation) const
{
    const auto Current = QueryThumbnail(Key);
    return Generation && Current.Ready && Current.Generation == Generation;
}

BrowserPreviewThumbnail BrowserPreviewRendering::QueryThumbnail(const std::string &Key) const
{
    if (!Allocation) return {};
    for (const auto &Slot : Allocation->Tiles)
        if (Slot.Key == Key && Slot.Ready) return {Slot.Generation, true};
    return {};
}

BrowserPreviewImport BrowserPreviewRendering::QueryImportImage(const std::string &Key) const
{
    if (!Allocation) return {};
    for (const auto &Slot : Allocation->Imports)
        if (Slot.Key == Key && Slot.Ready)
            return {Slot.Generation, Slot.Width, Slot.Height, true, {}};
    for (const auto &Slot : Allocation->Imports)
        if (Slot.Key == Key) return {Slot.Generation, 0, 0, false, Slot.Diagnostic};
    return {};
}

VkImage BrowserPreviewRendering::QueryLiveImage() const noexcept
{
    return Allocation && Allocation->Live ? Allocation->Live->Resolve.Image : VK_NULL_HANDLE;
}

VkImageView BrowserPreviewRendering::QueryLiveView() const noexcept
{
    return Allocation && Allocation->Live ? Allocation->Live->Resolve.View : VK_NULL_HANDLE;
}

VkImage BrowserPreviewRendering::QueryThumbnailImage(const std::string &Key) const noexcept
{
    if (!Allocation) return VK_NULL_HANDLE;
    for (const auto &Slot : Allocation->Tiles)
        if (Slot.Key == Key && Slot.Target) return Slot.Target->Resolve.Image;
    return VK_NULL_HANDLE;
}

VkImageView BrowserPreviewRendering::QueryThumbnailView(const std::string &Key) const noexcept
{
    if (!Allocation) return VK_NULL_HANDLE;
    for (const auto &Slot : Allocation->Tiles)
        if (Slot.Key == Key && Slot.Target) return Slot.Target->Resolve.View;
    return VK_NULL_HANDLE;
}

VkImage BrowserPreviewRendering::QueryImportVkImage(const std::string &Key) const noexcept
{
    if (!Allocation) return VK_NULL_HANDLE;
    for (const auto &Slot : Allocation->Imports)
        if (Slot.Key == Key) return Slot.Image.Image;
    return VK_NULL_HANDLE;
}

VkImageView BrowserPreviewRendering::QueryImportView(const std::string &Key) const noexcept
{
    if (!Allocation) return VK_NULL_HANDLE;
    for (const auto &Slot : Allocation->Imports)
        if (Slot.Key == Key) return Slot.Image.View;
    return VK_NULL_HANDLE;
}

bool BrowserPreviewRendering::QueryLiveReady() const noexcept
{
    return Allocation && Allocation->LiveReady;
}

const std::string &BrowserPreviewRendering::QueryDiagnostic() const noexcept
{
    static const std::string Empty;
    return Allocation ? Allocation->Diagnostic : Empty;
}
} // namespace Frontier
