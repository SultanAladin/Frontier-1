//============================================================================================================================================
//                                                               OVERLAYPASS.CPP
//============================================================================================================================================

#include "SlateVulkan/Device/WorkspaceOverlayPass/Api/WorkspaceOverlayPass.h"

namespace Slate
{

namespace
{

// 📐 The shader's record shapes, mirroring `WorkspaceOverlayVertex.slang` exactly — the byte offsets are the
//    guarantee, so each record is written here and read there, and the alignment assertions below are
//    what keep the two from drifting.
struct LineRecord
{
    float X0, Y0;             // [px]
    float X1, Y1;             // [px]
    float Thickness;          // [px]
    float Padding[3];         // [-] - alignment to 48 bytes
    float R, G, B, A;         // [-] - straight alpha, 0..1
};

struct DotRecord
{
    float X, Y;               // [px]
    float Radius;             // [px]
    float Padding;            // [-] - alignment
    float R, G, B, A;         // [-] - straight alpha, 0..1
};

struct TriangleRecord
{
    float X0, Y0;             // [px]
    float X1, Y1;             // [px]
    float X2, Y2;             // [px]
    float P0, P1;             // [-] - alignment
    float R, G, B, A;         // [-] - straight alpha, 0..1
};

static_assert(sizeof(LineRecord)     % 16u == 0u, "the line record must align to 16 bytes");
static_assert(sizeof(DotRecord)      % 16u == 0u, "the dot record must align to 16 bytes");
static_assert(sizeof(TriangleRecord) % 16u == 0u, "the triangle record must align to 16 bytes");

// 📐 16 bytes of mode/display plus six vec4 of camera plus one vec4 of leaf rect.
//    🔴 Vulkan guarantees at least 128 bytes of push constant; this is exactly 128.
constexpr std::uint32_t OverlayPushConstantBytes = 128u;

enum class WorkspaceOverlayDraw : std::uint32_t
{
    Line       = 0u,
    Dot        = 1u,
    Triangle   = 2u,
    GroundGrid = 3u
};

constexpr std::uint32_t DrawValue(WorkspaceOverlayDraw Draw)
{
    return static_cast<std::uint32_t>(Draw);
}

}   // namespace

WorkspaceOverlayPass::~WorkspaceOverlayPass()
{
    Reclaim();
}

Deliver<bool> WorkspaceOverlayPass::ConstructWorkspaceOverlayPass(const VulkanExchange&      Exchange,
                                     const DiagnosticExtension& Naming,
                                     ShaderCodec&               Streams,
                                     VkFormat                   ColourFormat)
{
    if (DeviceEdge != nullptr)
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "an overlay pass construction already stands" });

    const VkDevice Active = Exchange.ActiveDevice();

    if (Active == VK_NULL_HANDLE || Exchange.GraphicsQueue() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;

    LineBytes     = LineCapacity     * static_cast<std::uint32_t>(sizeof(LineRecord));
    DotBytes      = DotCapacity      * static_cast<std::uint32_t>(sizeof(DotRecord));
    TriangleBytes = TriangleCapacity * static_cast<std::uint32_t>(sizeof(TriangleRecord));

    // ① The two stages, resolved through the shader codec's own SPIR-V reading. A stream the build
    //    never lowered (the sandbox) refuses here, and the pass simply does not stand — the host
    //    reports it and runs without the overlay.
    const Deliver<std::uint32_t> VertexModule =
        Streams.Resolve("SlateVulkan", "WorkspaceOverlayVertex");

    if (!VertexModule.Resolved)
    {
        Reclaim();
        return Deliver<bool>::Refuse(VertexModule.Error);
    }

    const Deliver<std::uint32_t> FragmentModule =
        Streams.Resolve("SlateVulkan", "WorkspaceOverlayFragment");

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

    // ② The descriptor set layout: the three record regions, read by the vertex stage as structured
    //    pools. No uniform buffer exists — the viewport size rides the push constant.
    VkDescriptorSetLayoutBinding DescriptorAssignments[3] = {};

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        DescriptorAssignments[Index].binding            = Index;
        DescriptorAssignments[Index].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        DescriptorAssignments[Index].descriptorCount    = 1u;
        DescriptorAssignments[Index].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
        DescriptorAssignments[Index].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo LayoutDeclaration = {};
    LayoutDeclaration.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutDeclaration.bindingCount = 3u;
    LayoutDeclaration.pBindings    = DescriptorAssignments;

    if (vkCreateDescriptorSetLayout(Active, &LayoutDeclaration, nullptr, &OverlayLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay layout was rejected" });
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 3u;

    VkDescriptorPoolCreateInfo PoolDeclaration = {};
    PoolDeclaration.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolDeclaration.maxSets       = 1u;
    PoolDeclaration.poolSizeCount = 1u;
    PoolDeclaration.pPoolSizes    = &PoolSize;

    if (vkCreateDescriptorPool(Active, &PoolDeclaration, nullptr, &OverlayPool) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay pool was rejected" });
    }

    VkDescriptorSetAllocateInfo SetDeclaration = {};
    SetDeclaration.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetDeclaration.descriptorPool     = OverlayPool;
    SetDeclaration.descriptorSetCount = 1u;
    SetDeclaration.pSetLayouts        = &OverlayLayout;

    if (vkAllocateDescriptorSets(Active, &SetDeclaration, &OverlaySet) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay set was rejected" });
    }

    // ③ The one vertex buffer, host-visible and coherent, mapped once — the upload is a memcpy.
    VkBufferCreateInfo BufferDeclaration = {};
    BufferDeclaration.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferDeclaration.size        = static_cast<VkDeviceSize>(LineBytes) + DotBytes + TriangleBytes;
    BufferDeclaration.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferDeclaration.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Active, &BufferDeclaration, nullptr, &VertexBuffer) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay extent was rejected" });
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Active, VertexBuffer, &MemoryRequirements);

    VkMemoryAllocateInfo Allocation = {};
    Allocation.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Allocation.allocationSize  = MemoryRequirements.size;

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
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay memory was rejected" });
    }

    const VkResult Mapped =
        vkMapMemory(Active, VertexMemory, 0u, VK_WHOLE_SIZE, 0u,
                    reinterpret_cast<void**>(&MappedSlot));

    if (Mapped != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the overlay extent would not map" });
    }

    // ④ The set writes the three regions of the one buffer.
    VkDescriptorBufferInfo BufferSegments[3] = {};

    BufferSegments[0].buffer = VertexBuffer;
    BufferSegments[0].offset = 0u;
    BufferSegments[0].range  = LineBytes;
    BufferSegments[1].buffer = VertexBuffer;
    BufferSegments[1].offset = LineBytes;
    BufferSegments[1].range  = DotBytes;
    BufferSegments[2].buffer = VertexBuffer;
    BufferSegments[2].offset = LineBytes + DotBytes;
    BufferSegments[2].range  = TriangleBytes;

    VkWriteDescriptorSet Writes[3] = {};

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        Writes[Index].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet           = OverlaySet;
        Writes[Index].dstBinding       = Index;
        Writes[Index].descriptorCount  = 1u;
        Writes[Index].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo      = &BufferSegments[Index];
    }

    vkUpdateDescriptorSets(Active, 3u, Writes, 0u, nullptr);

    // ⑤ The pipeline layout: the set and the 16-byte push constant (mode + display size).
    VkPushConstantRange PushRange = {};
    // 🔴 The FRAGMENT stage reads the camera terms for mode 3, so the range must
    //    cover both stages — a vertex-only range makes the fragment read garbage.
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = OverlayPushConstantBytes;

    VkPipelineLayoutCreateInfo LayoutDeclaration2 = {};
    LayoutDeclaration2.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutDeclaration2.setLayoutCount         = 1u;
    LayoutDeclaration2.pSetLayouts            = &OverlayLayout;
    LayoutDeclaration2.pushConstantRangeCount = 1u;
    LayoutDeclaration2.pPushConstantRanges    = &PushRange;

    if (vkCreatePipelineLayout(Active, &LayoutDeclaration2, nullptr, &OverlayPipelineLayout) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay program layout was rejected" });
    }

    // ⑥ The program — dynamic rendering, so the colour attachment rides the pipeline-rendering info
    //    and no render construct stands. Straight-alpha blend; no depth; no cull.
    VkPipelineRenderingCreateInfo Rendering = {};
    Rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    Rendering.colorAttachmentCount    = 1u;
    Rendering.pColorAttachmentFormats = &ColourFormat;

    VkPipelineShaderStageCreateInfo Stages[2] = { VertexRead.Resolve(), FragmentRead.Resolve() };

    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // 🔴 No vertex input is declared and none may be: every vertex is synthesised from `SV_VertexID`
    //    and the structured pools, so an input declaration would be a layout the draw never satisfies.

    VkPipelineInputAssemblyStateCreateInfo Assembly = {};
    Assembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    Assembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo ViewportState = {};
    ViewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportState.viewportCount = 1u;
    ViewportState.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo Raster = {};
    Raster.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Raster.depthClampEnable        = VK_FALSE;
    Raster.rasterizerDiscardEnable = VK_FALSE;
    Raster.polygonMode             = VK_POLYGON_MODE_FILL;
    Raster.cullMode                = VK_CULL_MODE_NONE;
    Raster.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.depthBiasEnable         = VK_FALSE;
    Raster.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState Blend = {};
    Blend.blendEnable         = VK_TRUE;
    // 🔴 Straight alpha — `src_alpha / one_minus_src_alpha` — never the premultiplied
    //    `one / one_minus_src_alpha` the interface uses. The premultiplied read is what washed a
    //    low-alpha line out over a bright sky; straight alpha keeps the hue at the declared coverage.
    Blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.colorBlendOp        = VK_BLEND_OP_ADD;
    Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.alphaBlendOp        = VK_BLEND_OP_ADD;
    Blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColourBlend = {};
    ColourBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColourBlend.attachmentCount = 1u;
    ColourBlend.pAttachments    = &Blend;

    VkDynamicState Dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo DynamicState = {};
    DynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicState.dynamicStateCount = 2u;
    DynamicState.pDynamicStates    = Dynamic;

    VkGraphicsPipelineCreateInfo PipelineDeclaration = {};
    PipelineDeclaration.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipelineDeclaration.pNext               = &Rendering;
    PipelineDeclaration.stageCount          = 2u;
    PipelineDeclaration.pStages             = Stages;
    PipelineDeclaration.pVertexInputState   = &VertexInput;
    PipelineDeclaration.pInputAssemblyState = &Assembly;
    PipelineDeclaration.pViewportState      = &ViewportState;
    PipelineDeclaration.pRasterizationState = &Raster;
    PipelineDeclaration.pMultisampleState   = &Multisample;
    PipelineDeclaration.pColorBlendState    = &ColourBlend;
    PipelineDeclaration.pDynamicState       = &DynamicState;
    PipelineDeclaration.layout              = OverlayPipelineLayout;
    PipelineDeclaration.renderPass          = VK_NULL_HANDLE;   // [-] - dynamic rendering declares no construct
    PipelineDeclaration.subpass             = 0u;

    const VkResult Constructed =
        vkCreateGraphicsPipelines(Active, VK_NULL_HANDLE, 1u, &PipelineDeclaration, nullptr,
                                  &OverlayPipeline);

    if (Constructed != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the overlay program was rejected" });
    }

    if (Naming.Declare(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(OverlayPipeline),
                       "WorkspaceOverlayPass.Pipeline") &&
        Naming.Declare(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(VertexBuffer),
                       "WorkspaceOverlayPass.Geometry"))
    {
        // 📝 Every object named; nothing else to do with the delivery.
    }

    return Deliver<bool>::Result(true);
}

void WorkspaceOverlayPass::Upload(const OverlayGeometry& Overlay, float PixelScale)
{
    // 📐 The ground is a pose, not geometry: copied whole, never tessellated. Its authored pixel fields
    //    are scaled here alongside the screen-space records; its cell and world extents stay in metres.
    const float PhysicalScale = PixelScale > 0.0f ? PixelScale : 1.0f;
    OverlayGround = Overlay.Ground;
    OverlayGround.LineWeight *= PhysicalScale;
    OverlayGround.DotRadius *= PhysicalScale;
    OverlayGround.OrthoScale *= PhysicalScale;

    if (DeviceEdge == nullptr || MappedSlot == nullptr)
        return;

    // 📐 The conversion is a straight field-for-field copy: the panel's 24-byte line, 16-byte dot and
    //    28-byte triangle become the shader's aligned 40/32/48-byte records. The alignment fills are
    //    written zero so a driver that reads them reads defined bytes.
    std::uint8_t* Cursor = MappedSlot;

    for (std::uint32_t Index = 0u; Index < Overlay.LineCount && Index < LineCapacity; ++Index)
    {
        const OverlayLine& Source = Overlay.Lines[Index];
        LineRecord& Written = *reinterpret_cast<LineRecord*>(Cursor);

        Written.X0 = Source.X0 * PhysicalScale;
        Written.Y0 = Source.Y0 * PhysicalScale;
        Written.X1 = Source.X1 * PhysicalScale;
        Written.Y1 = Source.Y1 * PhysicalScale;
        Written.Thickness = Source.Thickness * PhysicalScale;
        Written.Padding[0] = 0.0f;
        Written.Padding[1] = 0.0f;
        Written.Padding[2] = 0.0f;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u)  & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u)  & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;

        Cursor += sizeof(LineRecord);
    }

    Cursor = MappedSlot + LineBytes;

    for (std::uint32_t Index = 0u; Index < Overlay.DotCount && Index < DotCapacity; ++Index)
    {
        const OverlayDot& Source = Overlay.Dots[Index];
        DotRecord& Written = *reinterpret_cast<DotRecord*>(Cursor);

        Written.X = Source.X * PhysicalScale;
        Written.Y = Source.Y * PhysicalScale;
        Written.Radius = Source.Radius * PhysicalScale;
        Written.Padding = 0.0f;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u)  & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u)  & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;

        Cursor += sizeof(DotRecord);
    }

    Cursor = MappedSlot + LineBytes + DotBytes;

    for (std::uint32_t Index = 0u; Index < Overlay.TriangleCount && Index < TriangleCapacity; ++Index)
    {
        const OverlayTriangle& Source = Overlay.Triangles[Index];
        TriangleRecord& Written = *reinterpret_cast<TriangleRecord*>(Cursor);

        Written.X0 = Source.X0 * PhysicalScale;
        Written.Y0 = Source.Y0 * PhysicalScale;
        Written.X1 = Source.X1 * PhysicalScale;
        Written.Y1 = Source.Y1 * PhysicalScale;
        Written.X2 = Source.X2 * PhysicalScale;
        Written.Y2 = Source.Y2 * PhysicalScale;
        Written.P0 = 0.0f;
        Written.P1 = 0.0f;
        Written.R = static_cast<float>((Source.Packed >> 16u) & 0xFFu) / 255.0f;
        Written.G = static_cast<float>((Source.Packed >> 8u)  & 0xFFu) / 255.0f;
        Written.B = static_cast<float>((Source.Packed >> 0u)  & 0xFFu) / 255.0f;
        Written.A = static_cast<float>((Source.Packed >> 24u) & 0xFFu) / 255.0f;

        Cursor += sizeof(TriangleRecord);
    }

    OverlayLineCount     = Overlay.LineCount;
    OverlayDotCount      = Overlay.DotCount;
    OverlayTriangleCount = Overlay.TriangleCount;
}

void WorkspaceOverlayPass::Record(VkCommandBuffer Command, std::uint32_t Width, std::uint32_t Height,
                          float LeafX0, float LeafY0, float LeafX1, float LeafY1,
                          float ScissorX0, float ScissorY0, float ScissorX1, float ScissorY1)
{
    if (DeviceEdge == nullptr || OverlayPipeline == VK_NULL_HANDLE || Command == VK_NULL_HANDLE ||
        Width == 0u || Height == 0u)
        return;

    // 📝 🔴 The device handle is NOT resolved here and must not be. `Record` issues command-buffer calls
    //    exclusively — bind, viewport, scissor, push, draw — and every one of them takes the recording rather
    //    than the device. The handle was a leftover of the CPU lattice's own upload path, which `85a5331`
    //    deleted; MSVC reported it as C4189 "initialized but not referenced" and it is removed rather than
    //    cast to void, because there is nothing here that wants a device.
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, OverlayPipeline);

    const VkViewport Viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(Width),
        .height   = static_cast<float>(Height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    // 🔴 The scissor IS the viewport leaf's box: the grid, the axes and the gizmo must never texture
    //    over the outliner, the properties or any other panel — they are drawn only inside the leaf
    //    that produced the geometry. The box is clamped to the display so a leaf at the window edge
    //    cannot push the scissor past the target.
    const std::int32_t ScissorX = static_cast<std::int32_t>(ScissorX0 < 0.0f ? 0.0f : ScissorX0);
    const std::int32_t ScissorY = static_cast<std::int32_t>(ScissorY0 < 0.0f ? 0.0f : ScissorY0);
    const std::int32_t ScissorRight  = static_cast<std::int32_t>(ScissorX1 > static_cast<float>(Width)
                                                                 ? static_cast<float>(Width) : ScissorX1);
    const std::int32_t ScissorBottom = static_cast<std::int32_t>(ScissorY1 > static_cast<float>(Height)
                                                                 ? static_cast<float>(Height) : ScissorY1);
    const std::int32_t ScissorWidth  = ScissorRight  > ScissorX ? ScissorRight  - ScissorX : 0;
    const std::int32_t ScissorHeight = ScissorBottom > ScissorY ? ScissorBottom - ScissorY : 0;

    const VkRect2D Scissor = {
        .offset = { ScissorX, ScissorY },
        .extent = { static_cast<std::uint32_t>(ScissorWidth),
                    static_cast<std::uint32_t>(ScissorHeight) }
    };

    vkCmdSetViewport(Command, 0u, 1u, &Viewport);
    vkCmdSetScissor(Command, 0u, 1u, &Scissor);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, OverlayPipelineLayout,
                            0u, 1u, &OverlaySet, 0u, nullptr);

    // 📐 The push constant rides the draw: mode 0 lines, 1 dots, 2 triangles, 3 ground; the display
    //    size and leaf rect in the same block are what the vertex stage transforms against.
    struct PushBlock
    {
        std::uint32_t Draw;
        float         DisplayWidth;
        float         DisplayHeight;
        float         FadeRadiusMetres;

        float EyeAndCell[4]     = {};
        float ForwardAndTanH[4] = {};
        float RightAndTanV[4]   = {};
        float UpAndPresent[4]   = {};
        float LatticeColour[4]  = {};
        float WeightsAndDot[4]  = {};
        float LeafRect[4]       = {};
    };

    const auto Stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 📐 THE ANALYTIC GROUND, FIRST. It is the furthest thing in the scene, and the
    //    pass blends in submission order with no depth buffer, so the lattice must
    //    be laid down before the gizmo and the axes that stand on it.
    if (OverlayGround.Standing)
    {
        // The spare scalar and lattice-alpha lane carry the two finite-grid distances without
        // increasing Vulkan's guaranteed-minimum 128-byte push-constant footprint.
        PushBlock Push = { DrawValue(WorkspaceOverlayDraw::GroundGrid),
                           static_cast<float>(Width), static_cast<float>(Height),
                           OverlayGround.FadeRadiusMetres };

        Push.EyeAndCell[0] = OverlayGround.EyeX;
        Push.EyeAndCell[1] = OverlayGround.EyeY;
        Push.EyeAndCell[2] = OverlayGround.EyeZ;
        Push.EyeAndCell[3] = OverlayGround.Cell;

        Push.ForwardAndTanH[0] = OverlayGround.ForwardX;
        Push.ForwardAndTanH[1] = OverlayGround.ForwardY;
        Push.ForwardAndTanH[2] = OverlayGround.ForwardZ;
        Push.ForwardAndTanH[3] = OverlayGround.TanHalfH;

        Push.RightAndTanV[0] = OverlayGround.RightX;
        Push.RightAndTanV[1] = OverlayGround.RightY;
        Push.RightAndTanV[2] = OverlayGround.RightZ;
        Push.RightAndTanV[3] = OverlayGround.TanHalfV;

        Push.UpAndPresent[0] = OverlayGround.UpX;
        Push.UpAndPresent[1] = OverlayGround.UpY;
        Push.UpAndPresent[2] = OverlayGround.UpZ;
        // 📐 Presentation in the low 8 bits, the orthographic scale in the rest as hundredths of a
        //    pixel per unit. The block is already at Vulkan's guaranteed-minimum 128 bytes.
        {
            // ⚠️ A float holds 24 bits of mantissa exactly. Beyond 65535 hundredths (a scale of
            //    ~655 px/unit) the packed value would start rounding, silently shifting the
            //    presentation bits underneath it, so the scale is clamped instead.
            constexpr std::uint32_t ScaleUnitLimit = 0xFFFFu;
            const std::uint32_t RequestedUnits = OverlayGround.OrthoScale > 0.0f
                ? static_cast<std::uint32_t>(OverlayGround.OrthoScale * 100.0f + 0.5f) : 0u;
            const std::uint32_t ScaleUnits = RequestedUnits > ScaleUnitLimit ? ScaleUnitLimit
                                                                             : RequestedUnits;
            Push.UpAndPresent[3] = static_cast<float>((OverlayGround.Presentation & 0xFFu)
                                                    | (ScaleUnits << 8u));
        }

        // 📐 The reference's own lattice tone, straight alpha.
        Push.LatticeColour[0] = 0.60f;
        Push.LatticeColour[1] = 0.65f;
        Push.LatticeColour[2] = 0.72f;
        Push.LatticeColour[3] = OverlayGround.ExtentMetres;

        Push.WeightsAndDot[0] = OverlayGround.LineWeight;
        Push.WeightsAndDot[1] = OverlayGround.DotRadius;
        Push.WeightsAndDot[2] = OverlayGround.Subdivisions;
        Push.WeightsAndDot[3] = static_cast<float>((OverlayGround.AxisMask & 0xFFu)
                                                 | ((OverlayGround.ViewedOrientation & 0xFFu) << 8u));

        // 🔴 THE WHOLE LEAF, NOT THE SCISSOR. The fragment stage maps the camera's field of view
        //    across this rectangle, so it must stay the leaf's true box however little of it is
        //    currently visible -- otherwise hiding a covered strip squashes the grid into what
        //    remains, which is exactly the defect that kept trading places with the vanishing grid.
        Push.LeafRect[0] = LeafX0;
        Push.LeafRect[1] = LeafY0;
        Push.LeafRect[2] = LeafX1;
        Push.LeafRect[3] = LeafY1;

        vkCmdPushConstants(Command, OverlayPipelineLayout, Stages,
                           0u, OverlayPushConstantBytes, &Push);

        // 🔴 SIX, not four. The pipeline declares `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, which consumes
        //    vertices in consecutive non-overlapping triples — four vertices assemble ONE triangle and
        //    drop the fourth, so the ground quad covered exactly half the leaf (measured: 60000 of
        //    120000 px on a 400 × 300 transcription). `QuadCorner` in the vertex stage maps these six
        //    onto the same four logical corners.
        vkCmdDraw(Command, 6u, 1u, 0u, 0u);
    }

    const auto DrawCategory = [&](WorkspaceOverlayDraw Draw, std::uint32_t Count,
                              std::uint32_t VerticesPerRecord)
    {
        if (Count == 0u)
            return;

        PushBlock Push = { DrawValue(Draw), static_cast<float>(Width), static_cast<float>(Height), 0.0f };
        // 📝 The leaf box again -- these vertices are already in leaf space.
        Push.LeafRect[0] = LeafX0;
        Push.LeafRect[1] = LeafY0;
        Push.LeafRect[2] = LeafX1;
        Push.LeafRect[3] = LeafY1;

        vkCmdPushConstants(Command, OverlayPipelineLayout, Stages,
                           0u, OverlayPushConstantBytes, &Push);
        vkCmdDraw(Command, Count * VerticesPerRecord, 1u, 0u, 0u);
    };

    // 🔴 Six vertices per quad record and three per triangle record. Lines and dots asked for four and
    //    the list topology then built every triangle from two records' corners: measured on three 300 px
    //    lines the MIDDLE line vanished entirely and rows 82…217 held a diagonal smear, and three dots
    //    covered 136 px where three whole dots are 192. That is the reported "lines render, dots don't".
    DrawCategory(WorkspaceOverlayDraw::Line, OverlayLineCount, 6u);
    DrawCategory(WorkspaceOverlayDraw::Dot, OverlayDotCount, 6u);
    DrawCategory(WorkspaceOverlayDraw::Triangle, OverlayTriangleCount, 3u);
}

void WorkspaceOverlayPass::Reclaim()
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

    if (OverlayPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Active, OverlayPipeline, nullptr);
    if (OverlayPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Active, OverlayPipelineLayout, nullptr);
    if (OverlayPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Active, OverlayPool, nullptr);
    if (OverlayLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Active, OverlayLayout, nullptr);
    if (VertexMemory != VK_NULL_HANDLE)
        vkFreeMemory(Active, VertexMemory, nullptr);
    if (VertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Active, VertexBuffer, nullptr);

    DeviceEdge            = nullptr;
    NamingEdge            = nullptr;
    VertexBuffer          = VK_NULL_HANDLE;
    VertexMemory          = VK_NULL_HANDLE;
    MappedSlot            = nullptr;
    OverlayLayout         = VK_NULL_HANDLE;
    OverlayPool           = VK_NULL_HANDLE;
    OverlaySet            = VK_NULL_HANDLE;
    OverlayPipelineLayout = VK_NULL_HANDLE;
    OverlayPipeline       = VK_NULL_HANDLE;
    LineBytes             = 0u;
    DotBytes              = 0u;
    TriangleBytes         = 0u;
    OverlayLineCount     = 0u;
    OverlayDotCount      = 0u;
    OverlayTriangleCount = 0u;
}


void WorkspaceOverlayPass::RecordAround(VkCommandBuffer Command, std::uint32_t Width, std::uint32_t Height,
                                        float CameraX0, float CameraY0, float CameraX1, float CameraY1,
                                        float ScissorX0, float ScissorY0, float ScissorX1, float ScissorY1,
                                        float WithheldX0, float WithheldY0, float WithheldX1, float WithheldY1)
{
    ExtentBand Bands[4] = {};
    const std::uint32_t Count = ExtentBandsAround(ScissorX0, ScissorY0, ScissorX1, ScissorY1,
                                                  WithheldX0, WithheldY0, WithheldX1, WithheldY1, Bands);

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        // ⚠️ The CAMERA rectangle is passed through untouched. Clipping it to the band is what made the
        //    grid squash into the space it had left instead of simply being hidden there.
        Record(Command, Width, Height,
               CameraX0, CameraY0, CameraX1, CameraY1,
               Bands[Index].MinimumX, Bands[Index].MinimumY,
               Bands[Index].MaximumX, Bands[Index].MaximumY);
    }
}

} // namespace Slate
