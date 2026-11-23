//============================================================================================================================================
//                                                       WORKSPACESCENEPASS.CPP
//============================================================================================================================================

#include "SlateVulkan/Device/WorkspaceScenePass/Api/WorkspaceScenePass.h"

#include <algorithm>
#include <cstring>

namespace Slate
{
namespace
{

struct SceneTriangleRecord
{
    float Position[9];
    float Colour[4];
    std::uint32_t MaterialSlot;
    float Padding[2];
};

struct SceneMaterialRecord
{
    float Albedo[4];
    float NormalIncidenceReflectance[4];
    float Scalars[4];
    float RefractionRatio;
    std::uint32_t ActiveChannelMask;
    std::uint32_t Closure;
    std::uint32_t Features;
    std::uint32_t Coverage;
    std::uint32_t Wall;
    std::uint32_t Interface;
    std::uint32_t TwoSided;
    std::uint32_t Reserved;
    float RegisterPadding[3];
    std::uint32_t DirtyLow;
    std::uint32_t DirtyHigh;
    std::uint32_t RecordPadding[2];
};

static_assert(sizeof(SceneTriangleRecord) % 16u == 0u, "scene triangle records must align to 16 bytes");
static_assert(sizeof(SceneMaterialRecord) % 16u == 0u, "scene material records must align to 16 bytes");

constexpr std::uint32_t ScenePushConstantBytes = 96u;

std::uint32_t ViewModeValue(WorkspaceSceneViewMode Mode)
{
    return static_cast<std::uint32_t>(Mode);
}

} // namespace

WorkspaceScenePass::~WorkspaceScenePass()
{
    Reclaim();
}

Deliver<bool> WorkspaceScenePass::ConstructWorkspaceScenePass(const VulkanExchange& Exchange,
                                                              const DiagnosticExtension& Naming,
                                                              ShaderCodec& Streams,
                                                              VkFormat ColourFormat)
{
    if (DeviceEdge != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a scene pass construction already stands" });

    const VkDevice Active = Exchange.ActiveDevice();
    if (Active == VK_NULL_HANDLE || Exchange.GraphicsQueue() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;
    TargetFormat = ColourFormat;

    TriangleBytes = TriangleCapacity * static_cast<std::uint32_t>(sizeof(SceneTriangleRecord));
    MaterialBytes = MaterialCapacity * static_cast<std::uint32_t>(sizeof(SceneMaterialRecord));

    const Deliver<std::uint32_t> VertexModule = Streams.Resolve("SlateVulkan", "WorkspaceSceneVertex");
    if (!VertexModule.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(VertexModule.Error);
    }

    const Deliver<std::uint32_t> FragmentModule = Streams.Resolve("SlateVulkan", "WorkspaceSceneFragment");
    if (!FragmentModule.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(FragmentModule.Error);
    }

    const Deliver<VkPipelineShaderStageCreateInfo> VertexRead =
        Streams.Stage(VertexModule.Resolve(), VK_SHADER_STAGE_VERTEX_BIT, {});
    if (!VertexRead.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(VertexRead.Error);
    }

    const Deliver<VkPipelineShaderStageCreateInfo> FragmentRead =
        Streams.Stage(FragmentModule.Resolve(), VK_SHADER_STAGE_FRAGMENT_BIT, {});
    if (!FragmentRead.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(FragmentRead.Error);
    }

    VkDescriptorSetLayoutBinding Bindings[2] = {};
    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        Bindings[Index].binding = Index;
        Bindings[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1u;
        Bindings[Index].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutDeclaration = {};
    LayoutDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutDeclaration.bindingCount = 2u;
    LayoutDeclaration.pBindings = Bindings;

    if (vkCreateDescriptorSetLayout(Active, &LayoutDeclaration, nullptr, &SceneLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene layout was rejected" });
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 2u;

    VkDescriptorPoolCreateInfo PoolDeclaration = {};
    PoolDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolDeclaration.maxSets = 1u;
    PoolDeclaration.poolSizeCount = 1u;
    PoolDeclaration.pPoolSizes = &PoolSize;

    if (vkCreateDescriptorPool(Active, &PoolDeclaration, nullptr, &ScenePool) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene pool was rejected" });
    }

    VkDescriptorSetAllocateInfo SetDeclaration = {};
    SetDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetDeclaration.descriptorPool = ScenePool;
    SetDeclaration.descriptorSetCount = 1u;
    SetDeclaration.pSetLayouts = &SceneLayout;

    if (vkAllocateDescriptorSets(Active, &SetDeclaration, &SceneSet) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene set was rejected" });
    }

    VkBufferCreateInfo BufferDeclaration = {};
    BufferDeclaration.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferDeclaration.size = static_cast<VkDeviceSize>(TriangleBytes) + MaterialBytes;
    BufferDeclaration.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferDeclaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Active, &BufferDeclaration, nullptr, &SceneBuffer) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene extent was rejected" });
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Active, SceneBuffer, &MemoryRequirements);

    VkMemoryAllocateInfo Allocation = {};
    Allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Allocation.allocationSize = MemoryRequirements.size;

    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(Exchange.ScoredDevice(), &MemoryProperties);

    bool MemoryTypeFound = false;
    for (std::uint32_t Index = 0u; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        const VkMemoryType& Candidate = MemoryProperties.memoryTypes[Index];
        if ((MemoryRequirements.memoryTypeBits & (1u << Index)) != 0u &&
            (Candidate.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u &&
            (Candidate.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u)
        {
            Allocation.memoryTypeIndex = Index;
            MemoryTypeFound = true;
            break;
        }
    }

    if (!MemoryTypeFound || vkAllocateMemory(Active, &Allocation, nullptr, &SceneMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Active, SceneBuffer, SceneMemory, 0u) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene memory was rejected" });
    }

    if (vkMapMemory(Active, SceneMemory, 0u, VK_WHOLE_SIZE, 0u,
                    reinterpret_cast<void**>(&MappedSlot)) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the scene extent would not map" });
    }

    VkDescriptorBufferInfo Triangles = { SceneBuffer, 0u, TriangleBytes };
    VkDescriptorBufferInfo Materials = { SceneBuffer, TriangleBytes, MaterialBytes };
    VkDescriptorBufferInfo BufferInfos[2] = { Triangles, Materials };

    VkWriteDescriptorSet Writes[2] = {};
    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet = SceneSet;
        Writes[Index].dstBinding = Index;
        Writes[Index].descriptorCount = 1u;
        Writes[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo = &BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Active, 2u, Writes, 0u, nullptr);

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset = 0u;
    PushRange.size = ScenePushConstantBytes;

    VkPipelineLayoutCreateInfo PipelineLayoutDeclaration = {};
    PipelineLayoutDeclaration.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipelineLayoutDeclaration.setLayoutCount = 1u;
    PipelineLayoutDeclaration.pSetLayouts = &SceneLayout;
    PipelineLayoutDeclaration.pushConstantRangeCount = 1u;
    PipelineLayoutDeclaration.pPushConstantRanges = &PushRange;

    if (vkCreatePipelineLayout(Active, &PipelineLayoutDeclaration, nullptr, &ScenePipelineLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene program layout was rejected" });
    }

    VkPipelineRenderingCreateInfo Rendering = {};
    Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    Rendering.colorAttachmentCount = 1u;
    Rendering.pColorAttachmentFormats = &ColourFormat;

    VkPipelineShaderStageCreateInfo Stages[2] = { VertexRead.Resolve(), FragmentRead.Resolve() };

    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo Assembly = {};
    Assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportState = {};
    ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportState.viewportCount = 1u;
    ViewportState.scissorCount = 1u;

    VkPipelineRasterizationStateCreateInfo Raster = {};
    Raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = {};
    Multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState Blend = {};
    Blend.blendEnable = VK_TRUE;
    Blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.colorBlendOp = VK_BLEND_OP_ADD;
    Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.alphaBlendOp = VK_BLEND_OP_ADD;
    Blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColourBlend = {};
    ColourBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColourBlend.attachmentCount = 1u;
    ColourBlend.pAttachments = &Blend;

    VkDynamicState Dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState = {};
    DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicState.dynamicStateCount = 2u;
    DynamicState.pDynamicStates = Dynamic;

    VkGraphicsPipelineCreateInfo PipelineDeclaration = {};
    PipelineDeclaration.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipelineDeclaration.pNext = &Rendering;
    PipelineDeclaration.stageCount = 2u;
    PipelineDeclaration.pStages = Stages;
    PipelineDeclaration.pVertexInputState = &VertexInput;
    PipelineDeclaration.pInputAssemblyState = &Assembly;
    PipelineDeclaration.pViewportState = &ViewportState;
    PipelineDeclaration.pRasterizationState = &Raster;
    PipelineDeclaration.pMultisampleState = &Multisample;
    PipelineDeclaration.pColorBlendState = &ColourBlend;
    PipelineDeclaration.pDynamicState = &DynamicState;
    PipelineDeclaration.layout = ScenePipelineLayout;
    PipelineDeclaration.renderPass = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(Active, VK_NULL_HANDLE, 1u, &PipelineDeclaration, nullptr,
                                  &ScenePipeline) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the scene program was rejected" });
    }

    Discard(Naming.Declare(VK_OBJECT_TYPE_PIPELINE,
                           reinterpret_cast<std::uint64_t>(ScenePipeline),
                           "WorkspaceScenePass.Pipeline"));
    Discard(Naming.Declare(VK_OBJECT_TYPE_BUFFER,
                           reinterpret_cast<std::uint64_t>(SceneBuffer),
                           "WorkspaceScenePass.GeometryAndMaterials"));

    return Deliver<bool>::Result(true);
}

void WorkspaceScenePass::Upload(const WorkspaceSceneTriangle* Triangles, std::uint32_t TriangleCount)
{
    UploadedTriangles.clear();
    UploadedTriangleCount = 0u;
    if (Triangles == nullptr || TriangleCount == 0u)
        return;

    UploadedTriangleCount = std::min(TriangleCount, TriangleCapacity);
    UploadedTriangles.assign(Triangles, Triangles + UploadedTriangleCount);

    if (MappedSlot == nullptr)
        return;

    std::uint8_t* Cursor = MappedSlot;
    for (std::uint32_t Index = 0u; Index < UploadedTriangleCount; ++Index)
    {
        const WorkspaceSceneTriangle& Source = UploadedTriangles[Index];
        SceneTriangleRecord& Written = *reinterpret_cast<SceneTriangleRecord*>(Cursor);
        std::memcpy(Written.Position, Source.Position, sizeof(Written.Position));
        std::memcpy(Written.Colour, Source.Colour, sizeof(Written.Colour));
        Written.MaterialSlot = Source.MaterialSlot;
        Written.Padding[0] = Written.Padding[1] = 0.0f;
        Cursor += sizeof(SceneTriangleRecord);
    }
}

void WorkspaceScenePass::UploadMaterials(const WorkspaceSceneMaterial* Materials, std::uint32_t MaterialCount)
{
    UploadedMaterials.clear();
    UploadedMaterialCount = 0u;
    if (Materials == nullptr || MaterialCount == 0u)
        return;

    UploadedMaterialCount = std::min(MaterialCount, MaterialCapacity);
    UploadedMaterials.assign(Materials, Materials + UploadedMaterialCount);

    if (MappedSlot == nullptr)
        return;

    std::uint8_t* Cursor = MappedSlot + TriangleBytes;
    for (std::uint32_t Index = 0u; Index < UploadedMaterialCount; ++Index)
    {
        const WorkspaceSceneMaterial& Source = UploadedMaterials[Index];
        SceneMaterialRecord& Written = *reinterpret_cast<SceneMaterialRecord*>(Cursor);
        std::memcpy(Written.Albedo, Source.Albedo, sizeof(Written.Albedo));
        std::memcpy(Written.NormalIncidenceReflectance, Source.NormalIncidenceReflectance,
                    sizeof(Written.NormalIncidenceReflectance));
        std::memcpy(Written.Scalars, Source.Scalars, sizeof(Written.Scalars));
        Written.RefractionRatio = Source.RefractionRatio;
        Written.ActiveChannelMask = Source.ActiveChannelMask;
        Written.Closure = Source.Closure;
        Written.Features = Source.Features;
        Written.Coverage = Source.Coverage;
        Written.Wall = Source.Wall;
        Written.Interface = Source.Interface;
        Written.TwoSided = Source.TwoSided;
        Written.Reserved = Source.Reserved;
        std::memcpy(Written.RegisterPadding, Source.RegisterPadding, sizeof(Written.RegisterPadding));
        Written.DirtyLow = static_cast<std::uint32_t>(Source.DirtyFingerprint & 0xFFFFFFFFull);
        Written.DirtyHigh = static_cast<std::uint32_t>(Source.DirtyFingerprint >> 32u);
        Written.RecordPadding[0] = Written.RecordPadding[1] = 0u;
        Cursor += sizeof(SceneMaterialRecord);
    }
}

void WorkspaceScenePass::Record(VkCommandBuffer Command, const WorkspaceSceneProjection& Projection,
                                float ClipX0, float ClipY0, float ClipX1, float ClipY1,
                                WorkspaceSceneViewMode ViewMode)
{
    if (!Standing() || Command == VK_NULL_HANDLE || UploadedTriangleCount == 0u)
        return;

    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, ScenePipeline);

    const float Width = Projection.DisplayWidth > 0.0f ? Projection.DisplayWidth : 1.0f;
    const float Height = Projection.DisplayHeight > 0.0f ? Projection.DisplayHeight : 1.0f;
    const float MinX = ClipX0 < ClipX1 ? ClipX0 : ClipX1;
    const float MaxX = ClipX0 < ClipX1 ? ClipX1 : ClipX0;
    const float MinY = ClipY0 < ClipY1 ? ClipY0 : ClipY1;
    const float MaxY = ClipY0 < ClipY1 ? ClipY1 : ClipY0;

    VkViewport Viewport = {};
    Viewport.x = MinX;
    Viewport.y = MinY;
    Viewport.width = MaxX - MinX;
    Viewport.height = MaxY - MinY;
    Viewport.minDepth = 0.0f;
    Viewport.maxDepth = 1.0f;

    const std::int32_t ScissorX = static_cast<std::int32_t>(std::max(0.0f, MinX));
    const std::int32_t ScissorY = static_cast<std::int32_t>(std::max(0.0f, MinY));
    const std::int32_t ScissorRight = static_cast<std::int32_t>(std::min(Width, MaxX));
    const std::int32_t ScissorBottom = static_cast<std::int32_t>(std::min(Height, MaxY));
    const VkRect2D Scissor = {
        { ScissorX, ScissorY },
        { static_cast<std::uint32_t>(ScissorRight > ScissorX ? ScissorRight - ScissorX : 0),
          static_cast<std::uint32_t>(ScissorBottom > ScissorY ? ScissorBottom - ScissorY : 0) }
    };

    vkCmdSetViewport(Command, 0u, 1u, &Viewport);
    vkCmdSetScissor(Command, 0u, 1u, &Scissor);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, ScenePipelineLayout,
                            0u, 1u, &SceneSet, 0u, nullptr);

    struct PushBlock
    {
        std::uint32_t ViewMode;
        std::uint32_t MaterialCount;
        float DisplayWidth;
        float DisplayHeight;
        float ViewProjection[16];
    };

    PushBlock Push = { ViewModeValue(ViewMode), UploadedMaterialCount, Width, Height, {} };
    for (std::uint32_t Index = 0u; Index < 16u; ++Index)
        Push.ViewProjection[Index] = Projection.ViewProjection[Index];

    vkCmdPushConstants(Command, ScenePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, ScenePushConstantBytes, &Push);

    const std::uint32_t VerticesPerTriangle = ViewMode == WorkspaceSceneViewMode::Points ? 18u : 3u;
    vkCmdDraw(Command, UploadedTriangleCount * VerticesPerTriangle, 1u, 0u, 0u);
}

void WorkspaceScenePass::Reclaim()
{
    if (DeviceEdge == nullptr)
    {
        UploadedTriangles.clear();
        UploadedMaterials.clear();
        UploadedTriangleCount = 0u;
        UploadedMaterialCount = 0u;
        return;
    }

    const VkDevice Active = DeviceEdge->ActiveDevice();
    if (Active == VK_NULL_HANDLE)
    {
        DeviceEdge = nullptr;
        NamingEdge = nullptr;
        return;
    }

    if (MappedSlot != nullptr)
    {
        vkUnmapMemory(Active, SceneMemory);
        MappedSlot = nullptr;
    }

    if (ScenePipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Active, ScenePipeline, nullptr);
    if (ScenePipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Active, ScenePipelineLayout, nullptr);
    if (ScenePool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Active, ScenePool, nullptr);
    if (SceneLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Active, SceneLayout, nullptr);
    if (SceneMemory != VK_NULL_HANDLE)
        vkFreeMemory(Active, SceneMemory, nullptr);
    if (SceneBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Active, SceneBuffer, nullptr);

    DeviceEdge = nullptr;
    NamingEdge = nullptr;
    TargetFormat = VK_FORMAT_UNDEFINED;
    SceneBuffer = VK_NULL_HANDLE;
    SceneMemory = VK_NULL_HANDLE;
    MappedSlot = nullptr;
    SceneLayout = VK_NULL_HANDLE;
    ScenePool = VK_NULL_HANDLE;
    SceneSet = VK_NULL_HANDLE;
    ScenePipelineLayout = VK_NULL_HANDLE;
    ScenePipeline = VK_NULL_HANDLE;
    TriangleBytes = 0u;
    MaterialBytes = 0u;
    UploadedTriangleCount = 0u;
    UploadedMaterialCount = 0u;
    UploadedTriangles.clear();
    UploadedMaterials.clear();
}

} // namespace Slate
