//============================================================================================================================================
//                                                        WORKSPACECADPASS.CPP
//============================================================================================================================================

#include "SlateVulkan/Device/WorkspaceCadPass/Api/WorkspaceCadPass.h"

namespace Slate
{

namespace
{

struct SegmentRecord
{
    float Along0, Across0;
    float Along1, Across1;
    float Thickness;
    float Padding[3];
    float R, G, B, A;
};

struct FillRecord
{
    float Along0, Across0;
    float Along1, Across1;
    float Along2, Across2;
    float Padding[2];
    float R, G, B, A;
};

struct MarkerRecord
{
    float Along, Across;
    float Radius;
    std::uint32_t Subject;
    float R, G, B, A;
};

static_assert(sizeof(SegmentRecord) % 16u == 0u, "the CAD segment record must align to 16 bytes");
static_assert(sizeof(FillRecord) % 16u == 0u, "the CAD fill record must align to 16 bytes");
static_assert(sizeof(MarkerRecord) % 16u == 0u, "the CAD marker record must align to 16 bytes");

constexpr std::uint32_t CadPushConstantBytes = 64u;

enum class WorkspaceCadDraw : std::uint32_t
{
    Segment = 0u,
    Fill = 1u,
    Marker = 2u
};

constexpr std::uint32_t DrawValue(WorkspaceCadDraw Subject)
{
    return static_cast<std::uint32_t>(Subject);
}

} // namespace

WorkspaceCadPass::~WorkspaceCadPass()
{
    Reclaim();
}

Deliver<bool> WorkspaceCadPass::ConstructWorkspaceCadPass(const VulkanExchange& Exchange,
                                                          const DiagnosticExtension& Naming,
                                                          ShaderCodec& Streams,
                                                          VkFormat ColourFormat)
{
    if (DeviceEdge != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "a CAD pass construction already stands" });

    const VkDevice Active = Exchange.ActiveDevice();
    if (Active == VK_NULL_HANDLE || Exchange.GraphicsQueue() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;

    SegmentBytes = SegmentCapacity * static_cast<std::uint32_t>(sizeof(SegmentRecord));
    FillBytes = FillCapacity * static_cast<std::uint32_t>(sizeof(FillRecord));
    MarkerBytes = MarkerCapacity * static_cast<std::uint32_t>(sizeof(MarkerRecord));

    const Deliver<std::uint32_t> VertexModule = Streams.Resolve("SlateVulkan", "WorkspaceCadVertex");
    if (!VertexModule.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(VertexModule.Error);
    }

    const Deliver<std::uint32_t> FragmentModule = Streams.Resolve("SlateVulkan", "WorkspaceCadFragment");
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

    VkDescriptorSetLayoutBinding Bindings[3] = {};
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        Bindings[Index].binding = Index;
        Bindings[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1u;
        Bindings[Index].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutDeclaration = {};
    LayoutDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutDeclaration.bindingCount = 3u;
    LayoutDeclaration.pBindings = Bindings;

    if (vkCreateDescriptorSetLayout(Active, &LayoutDeclaration, nullptr, &CadLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD layout was rejected" });
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 3u;

    VkDescriptorPoolCreateInfo PoolDeclaration = {};
    PoolDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolDeclaration.maxSets = 1u;
    PoolDeclaration.poolSizeCount = 1u;
    PoolDeclaration.pPoolSizes = &PoolSize;

    if (vkCreateDescriptorPool(Active, &PoolDeclaration, nullptr, &CadPool) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD pool was rejected" });
    }

    VkDescriptorSetAllocateInfo SetDeclaration = {};
    SetDeclaration.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetDeclaration.descriptorPool = CadPool;
    SetDeclaration.descriptorSetCount = 1u;
    SetDeclaration.pSetLayouts = &CadLayout;

    if (vkAllocateDescriptorSets(Active, &SetDeclaration, &CadSet) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD set was rejected" });
    }

    VkBufferCreateInfo BufferDeclaration = {};
    BufferDeclaration.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferDeclaration.size = static_cast<VkDeviceSize>(SegmentBytes) + FillBytes + MarkerBytes;
    BufferDeclaration.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferDeclaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Active, &BufferDeclaration, nullptr, &VertexBuffer) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD extent was rejected" });
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Active, VertexBuffer, &MemoryRequirements);

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

    if (!MemoryTypeFound || vkAllocateMemory(Active, &Allocation, nullptr, &VertexMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Active, VertexBuffer, VertexMemory, 0u) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD memory was rejected" });
    }

    if (vkMapMemory(Active, VertexMemory, 0u, VK_WHOLE_SIZE, 0u,
                    reinterpret_cast<void**>(&MappedSlot)) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the CAD extent would not map" });
    }

    VkDescriptorBufferInfo Segments = { VertexBuffer, 0u, SegmentBytes };
    VkDescriptorBufferInfo Fills = { VertexBuffer, SegmentBytes, FillBytes };
    VkDescriptorBufferInfo Markers = { VertexBuffer, SegmentBytes + FillBytes, MarkerBytes };
    VkDescriptorBufferInfo BufferInfos[3] = { Segments, Fills, Markers };

    VkWriteDescriptorSet Writes[3] = {};
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet = CadSet;
        Writes[Index].dstBinding = Index;
        Writes[Index].descriptorCount = 1u;
        Writes[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo = &BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Active, 3u, Writes, 0u, nullptr);

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset = 0u;
    PushRange.size = CadPushConstantBytes;

    VkPipelineLayoutCreateInfo PipelineLayoutDeclaration = {};
    PipelineLayoutDeclaration.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipelineLayoutDeclaration.setLayoutCount = 1u;
    PipelineLayoutDeclaration.pSetLayouts = &CadLayout;
    PipelineLayoutDeclaration.pushConstantRangeCount = 1u;
    PipelineLayoutDeclaration.pPushConstantRanges = &PushRange;

    if (vkCreatePipelineLayout(Active, &PipelineLayoutDeclaration, nullptr, &CadPipelineLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD program layout was rejected" });
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
    PipelineDeclaration.layout = CadPipelineLayout;
    PipelineDeclaration.renderPass = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(Active, VK_NULL_HANDLE, 1u, &PipelineDeclaration, nullptr,
                                  &CadPipeline) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the CAD program was rejected" });
    }

    Discard(Naming.Declare(VK_OBJECT_TYPE_PIPELINE,
                           reinterpret_cast<std::uint64_t>(CadPipeline),
                           "WorkspaceCadPass.Pipeline"));
    Discard(Naming.Declare(VK_OBJECT_TYPE_BUFFER,
                           reinterpret_cast<std::uint64_t>(VertexBuffer),
                           "WorkspaceCadPass.Geometry"));

    return Deliver<bool>::Result(true);
}

void WorkspaceCadPass::Upload(const WorkspaceCadPacket& Packet)
{
    if (DeviceEdge == nullptr || MappedSlot == nullptr)
        return;

    // This pass owns one persistently mapped buffer, while the frame scheduler permits several
    // command buffers to remain in flight. Waiting before replacing the packet prevents a later CPU
    // write from racing a previous frame's vertex fetch. This is deliberately limited to packet
    // changes (the host fingerprints unchanged packets and does not call Upload for idle frames).
    const VkDevice Active = DeviceEdge->ActiveDevice();
    if (Active == VK_NULL_HANDLE)
        return;
    static_cast<void>(vkDeviceWaitIdle(Active));

    std::uint8_t* Cursor = MappedSlot;
    for (std::uint32_t Index = 0u; Index < Packet.SegmentCount && Index < SegmentCapacity; ++Index)
    {
        const WorkspaceCadSegment& Source = Packet.Segments[Index];
        SegmentRecord& Written = *reinterpret_cast<SegmentRecord*>(Cursor);
        Written.Along0 = Source.Along0;
        Written.Across0 = Source.Across0;
        Written.Along1 = Source.Along1;
        Written.Across1 = Source.Across1;
        Written.Thickness = Source.Thickness;
        Written.Padding[0] = Written.Padding[1] = Written.Padding[2] = 0.0f;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u) & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u) & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;
        Cursor += sizeof(SegmentRecord);
    }

    Cursor = MappedSlot + SegmentBytes;
    for (std::uint32_t Index = 0u; Index < Packet.FillCount && Index < FillCapacity; ++Index)
    {
        const WorkspaceCadFillTriangle& Source = Packet.Fills[Index];
        FillRecord& Written = *reinterpret_cast<FillRecord*>(Cursor);
        Written.Along0 = Source.Along0;
        Written.Across0 = Source.Across0;
        Written.Along1 = Source.Along1;
        Written.Across1 = Source.Across1;
        Written.Along2 = Source.Along2;
        Written.Across2 = Source.Across2;
        Written.Padding[0] = Written.Padding[1] = 0.0f;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u) & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u) & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;
        Cursor += sizeof(FillRecord);
    }

    Cursor = MappedSlot + SegmentBytes + FillBytes;
    for (std::uint32_t Index = 0u; Index < Packet.MarkerCount && Index < MarkerCapacity; ++Index)
    {
        const WorkspaceCadMarker& Source = Packet.Markers[Index];
        MarkerRecord& Written = *reinterpret_cast<MarkerRecord*>(Cursor);
        Written.Along = Source.Along;
        Written.Across = Source.Across;
        Written.Radius = Source.Radius;
        Written.Subject = Source.Subject;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u) & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u) & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;
        Cursor += sizeof(MarkerRecord);
    }

    PacketMinimumAlong = Packet.ExtentStanding ? Packet.MinimumAlong : -1.0f;
    PacketMinimumAcross = Packet.ExtentStanding ? Packet.MinimumAcross : -1.0f;
    PacketMaximumAlong = Packet.ExtentStanding ? Packet.MaximumAlong : 1.0f;
    PacketMaximumAcross = Packet.ExtentStanding ? Packet.MaximumAcross : 1.0f;
    // Keep draw counts identical to the records actually copied into the mapped buffer. A malformed
    // or temporarily over-capacity packet must not make the GPU read beyond the uploaded ranges.
    PacketSegmentCount = Packet.SegmentCount < SegmentCapacity ? Packet.SegmentCount : SegmentCapacity;
    PacketFillCount = Packet.FillCount < FillCapacity ? Packet.FillCount : FillCapacity;
    PacketMarkerCount = Packet.MarkerCount < MarkerCapacity ? Packet.MarkerCount : MarkerCapacity;
}

void WorkspaceCadPass::Record(VkCommandBuffer Command, const WorkspaceCadProjection& Projection,
                              float ClipX0, float ClipY0, float ClipX1, float ClipY1)
{
    if (DeviceEdge == nullptr || CadPipeline == VK_NULL_HANDLE || Command == VK_NULL_HANDLE ||
        Projection.DisplayWidth <= 0.0f || Projection.DisplayHeight <= 0.0f)
        return;

    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, CadPipeline);

    const VkViewport Viewport = { 0.0f, 0.0f, Projection.DisplayWidth, Projection.DisplayHeight, 0.0f, 1.0f };

    const std::int32_t ScissorX = static_cast<std::int32_t>(ClipX0 < 0.0f ? 0.0f : ClipX0);
    const std::int32_t ScissorY = static_cast<std::int32_t>(ClipY0 < 0.0f ? 0.0f : ClipY0);
    const std::int32_t ScissorRight = static_cast<std::int32_t>(ClipX1 > Projection.DisplayWidth
                                                              ? Projection.DisplayWidth : ClipX1);
    const std::int32_t ScissorBottom = static_cast<std::int32_t>(ClipY1 > Projection.DisplayHeight
                                                               ? Projection.DisplayHeight : ClipY1);
    const VkRect2D Scissor = {
        { ScissorX, ScissorY },
        { static_cast<std::uint32_t>(ScissorRight > ScissorX ? ScissorRight - ScissorX : 0),
          static_cast<std::uint32_t>(ScissorBottom > ScissorY ? ScissorBottom - ScissorY : 0) }
    };

    vkCmdSetViewport(Command, 0u, 1u, &Viewport);
    vkCmdSetScissor(Command, 0u, 1u, &Scissor);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, CadPipelineLayout,
                            0u, 1u, &CadSet, 0u, nullptr);

    struct PushBlock
    {
        std::uint32_t Draw;
        float DisplayWidth;
        float DisplayHeight;
        float Padding;
        float Projection0[4] = {};
        float Projection1[4] = {};
        float Projection2[4] = {};
    };

    const auto Stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const auto DrawCategory = [&](WorkspaceCadDraw Draw, std::uint32_t Count, std::uint32_t VerticesPerRecord)
    {
        if (Count == 0u)
            return;

        PushBlock Push = { DrawValue(Draw), Projection.DisplayWidth, Projection.DisplayHeight, 0.0f };
        for (std::uint32_t Index = 0u; Index < 4u; ++Index)
        {
            Push.Projection0[Index] = Projection.Projection0[Index];
            Push.Projection1[Index] = Projection.Projection1[Index];
            Push.Projection2[Index] = Projection.Projection2[Index];
        }

        vkCmdPushConstants(Command, CadPipelineLayout, Stages, 0u, CadPushConstantBytes, &Push);
        vkCmdDraw(Command, Count * VerticesPerRecord, 1u, 0u, 0u);
    };

    // 🔴 A near-clipped fill can become a QUAD. The vertex shader triangulates that into two
    //    triangles per record, so fills now spend six vertices just as segments and markers do.
    DrawCategory(WorkspaceCadDraw::Fill, PacketFillCount, 6u);
    DrawCategory(WorkspaceCadDraw::Segment, PacketSegmentCount, 6u);
    DrawCategory(WorkspaceCadDraw::Marker, PacketMarkerCount, 6u);
}

void WorkspaceCadPass::Reclaim()
{
    if (DeviceEdge == nullptr)
        return;

    const VkDevice Active = DeviceEdge->ActiveDevice();
    if (Active == VK_NULL_HANDLE)
    {
        DeviceEdge = nullptr;
        NamingEdge = nullptr;
        return;
    }

    if (MappedSlot != nullptr)
    {
        vkUnmapMemory(Active, VertexMemory);
        MappedSlot = nullptr;
    }

    if (CadPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Active, CadPipeline, nullptr);
    if (CadPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Active, CadPipelineLayout, nullptr);
    if (CadPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Active, CadPool, nullptr);
    if (CadLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Active, CadLayout, nullptr);
    if (VertexMemory != VK_NULL_HANDLE)
        vkFreeMemory(Active, VertexMemory, nullptr);
    if (VertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Active, VertexBuffer, nullptr);

    DeviceEdge = nullptr;
    NamingEdge = nullptr;
    VertexBuffer = VK_NULL_HANDLE;
    VertexMemory = VK_NULL_HANDLE;
    MappedSlot = nullptr;
    CadLayout = VK_NULL_HANDLE;
    CadPool = VK_NULL_HANDLE;
    CadSet = VK_NULL_HANDLE;
    CadPipelineLayout = VK_NULL_HANDLE;
    CadPipeline = VK_NULL_HANDLE;
    SegmentBytes = 0u;
    FillBytes = 0u;
    MarkerBytes = 0u;
    PacketMinimumAlong = -1.0f;
    PacketMinimumAcross = -1.0f;
    PacketMaximumAlong = 1.0f;
    PacketMaximumAcross = 1.0f;
    PacketSegmentCount = 0u;
    PacketFillCount = 0u;
    PacketMarkerCount = 0u;
}


void WorkspaceCadPass::RecordAround(VkCommandBuffer Command, const WorkspaceCadProjection& Projection,
                                    float ClipX0, float ClipY0, float ClipX1, float ClipY1,
                                    float WithheldX0, float WithheldY0, float WithheldX1, float WithheldY1)
{
    ExtentBand Bands[4] = {};
    const std::uint32_t Count = ExtentBandsAround(ClipX0, ClipY0, ClipX1, ClipY1,
                                                  WithheldX0, WithheldY0, WithheldX1, WithheldY1, Bands);

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        Record(Command, Projection,
               Bands[Index].MinimumX, Bands[Index].MinimumY,
               Bands[Index].MaximumX, Bands[Index].MaximumY);
    }
}

} // namespace Slate
