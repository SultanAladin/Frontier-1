//============================================================================================================================================
//                                                             PROGRAMINDEX.CPP
//============================================================================================================================================
// 🧩 The layout every program reaches through, the two construction routes, and the reclamation that returns both.

#include "SlateVulkan/Device/ProgramIndex/Api/ProgramIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ProgramIndex::ConstructProgramIndex(const VulkanExchange&      Exchange,
                                      ShaderCodec&               Modules,
                                      const DescriptorIndex&     Descriptors,
                                      const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge     = &Exchange;
    ModuleEdge     = &Modules;
    DescriptorEdge = &Descriptors;
    NamingEdge     = &Naming;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REACH
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkPipelineLayout> ProgramIndex::ReachLayout(const std::vector<std::uint32_t>&  LayoutIndexs,
                                                    std::uint32_t                     ConstantBytes,
                                                    VkShaderStageFlags                ReachingStages)
{
    std::vector<VkDescriptorSetLayout> Reached;
    Reached.reserve(LayoutIndexs.size());

    // 📝 Resolved in the order given, because the position in this run **is** the set ordinal the shader
    //    declares. Sorting them or skipping an unresolved one would leave every later set addressed one
    //    position from where the shader reads it, which the vendor reports as a set that was never written.
    for (const std::uint32_t LayoutIndex : LayoutIndexs)
    {
        const Deliver<VkDescriptorSetLayout> Declared = DescriptorEdge->Layout(LayoutIndex);

        if (!Declared.Resolved)
            return Deliver<VkPipelineLayout>::Refuse(Declared.Error);

        Reached.push_back(Declared.Resolve());
    }

    VkPushConstantRange Recorded = {};
    Recorded.stageFlags          = ReachingStages;
    Recorded.offset              = 0u;
    Recorded.size                = ConstantBytes;

    VkPipelineLayoutCreateInfo Declaration = {};
    Declaration.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    Declaration.setLayoutCount             = static_cast<std::uint32_t>(Reached.size());
    Declaration.pSetLayouts                = Reached.empty() ? nullptr : Reached.data();
    Declaration.pushConstantRangeCount     = ConstantBytes > 0u ? 1u : 0u;
    Declaration.pPushConstantRanges        = ConstantBytes > 0u ? &Recorded : nullptr;

    VkPipelineLayout Constructed = VK_NULL_HANDLE;

    if (vkCreatePipelineLayout(DeviceEdge->ActiveDevice(), &Declaration, nullptr, &Constructed) != VK_SUCCESS)
        return Deliver<VkPipelineLayout>::Refuse({ RefusalReason::HostDenied, "the device rejected the program layout" });

    return Deliver<VkPipelineLayout>::Result(Constructed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GRAPHICS ROUTE
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ProgramIndex::DeclareGraphics(const GraphicsDeclaration& Declaring)
{
    if (DeviceEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "no device was taken" });

    if (Declaring.RenderConstruct == VK_NULL_HANDLE)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a graphics program names no render construct" });
    }

    const Deliver<VkPipelineShaderStageCreateInfo> VertexRead =
        ModuleEdge->Stage(Declaring.VertexModule, VK_SHADER_STAGE_VERTEX_BIT, Declaring.VertexFixed);

    if (!VertexRead.Resolved)
        return Deliver<std::uint32_t>::Refuse(VertexRead.Error);

    const Deliver<VkPipelineShaderStageCreateInfo> FragmentRead =
        ModuleEdge->Stage(Declaring.FragmentModule, VK_SHADER_STAGE_FRAGMENT_BIT, Declaring.FragmentFixed);

    if (!FragmentRead.Resolved)
        return Deliver<std::uint32_t>::Refuse(FragmentRead.Error);

    const VkPipelineShaderStageCreateInfo Reading[2] = { VertexRead.Resolve(), FragmentRead.Resolve() };

    const Deliver<VkPipelineLayout> Reached =
        ReachLayout(Declaring.LayoutIndexs,
                    Declaring.ConstantBytes,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!Reached.Resolved)
        return Deliver<std::uint32_t>::Refuse(Reached.Error);

    const VkPipelineLayout ReachedLayout = Reached.Resolve();

    // 🔴 No vertex input declaration. `16` §4's raster reads its positions and its ordinals from declared spans
    //    by the vertex ordinal, so a fixed-function input declaration would describe an arrangement nothing
    //    supplies — and the vendor accepts an empty one without remark, which is what makes stating this here
    //    worth more than the declaration itself.
    VkPipelineVertexInputStateCreateInfo Supplied = {};
    Supplied.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo Assembled = {};
    Assembled.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    Assembled.topology                               = Declaring.Assembled;
    Assembled.primitiveRestartEnable                 = VK_FALSE;

    // 📝 🔴 The display extent is recorded rather than constructed against. `06` §7 reclaims and re-claims every
    //    display-relative target on an extent change, and a program carrying the extent would have to be
    //    reconstructed with them — which is a device stall on every drag sample. The recording writes the
    //    extent it opened against; the counts here are what the vendor requires and nothing more.
    VkPipelineViewportStateCreateInfo Extent = {};
    Extent.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    Extent.viewportCount                     = 1u;
    Extent.scissorCount                      = 1u;

    const VkDynamicState Recorded[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo Amended = {};
    Amended.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    Amended.dynamicStateCount                = 2u;
    Amended.pDynamicStates                   = Recorded;

    // ⚠️ The winding is counter-clockwise because `38`'s conditioning orients every face that way and `16`'s
    //    orientation cone is derived from those same orientations. A program declaring the opposite winding
    //    culls exactly the faces the cone accepted, and the partition disappears while the cull reports it
    //    visible — two mechanisms disagreeing with no operand between them to compare.
    VkPipelineRasterizationStateCreateInfo Resolved = {};
    Resolved.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Resolved.polygonMode                            = VK_POLYGON_MODE_FILL;
    Resolved.cullMode                               = Declaring.FacingCulled;
    Resolved.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Resolved.lineWidth                              = 1.0f;

    VkPipelineMultisampleStateCreateInfo Sampled = {};
    Sampled.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Sampled.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo Compared = {};
    Compared.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    Compared.depthTestEnable                       = Declaring.Depth.DepthTested  ? VK_TRUE : VK_FALSE;
    Compared.depthWriteEnable                      = Declaring.Depth.DepthWritten ? VK_TRUE : VK_FALSE;
    Compared.depthCompareOp                        = Declaring.Depth.DepthComparison;
    Compared.minDepthBounds                        = 0.0f;
    Compared.maxDepthBounds                        = 1.0f;

    // 📝 One entry per colour attachment, every one of them writing all four components and combining with
    //    nothing. `08` §2 declares every target `16` produces as written whole, so there is nothing standing
    //    in one to combine against.
    std::vector<VkPipelineColorBlendAttachmentState> Written(
        static_cast<std::size_t>(Declaring.ColourAttachmentCount));

    for (VkPipelineColorBlendAttachmentState& Component : Written)
    {
        Component.blendEnable    = VK_FALSE;
        Component.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                 | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo Combined = {};
    Combined.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    Combined.attachmentCount                     = Declaring.ColourAttachmentCount;
    Combined.pAttachments                        = Written.empty() ? nullptr : Written.data();

    VkGraphicsPipelineCreateInfo Declaration = {};
    Declaration.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Declaration.stageCount                   = 2u;
    Declaration.pStages                      = Reading;
    Declaration.pVertexInputState            = &Supplied;
    Declaration.pInputAssemblyState          = &Assembled;
    Declaration.pViewportState               = &Extent;
    Declaration.pRasterizationState          = &Resolved;
    Declaration.pMultisampleState            = &Sampled;
    Declaration.pDepthStencilState           = &Compared;
    Declaration.pColorBlendState             = &Combined;
    Declaration.pDynamicState                = &Amended;
    Declaration.layout                       = ReachedLayout;
    Declaration.renderPass                   = Declaring.RenderConstruct;
    Declaration.subpass                      = 0u;

    VkPipeline Constructed = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(DeviceEdge->ActiveDevice(), VK_NULL_HANDLE, 1u, &Declaration, nullptr, &Constructed)
        != VK_SUCCESS)
    {
        // 🔴 The layout is destroyed before the refusal returns. It was constructed inside this call and
        //    nothing outside it holds a reference, so a refusal that left it standing would leak one vendor
        //    layout per rejected program — and a rejected program is exactly the case a caller retries.
        vkDestroyPipelineLayout(DeviceEdge->ActiveDevice(), ReachedLayout, nullptr);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::HostDenied, "the device rejected the graphics program" });
    }

    HeldProgram Held;
    Held.Constructed   = Constructed;
    Held.ReachedLayout = ReachedLayout;
    Held.RecordedAs    = VK_PIPELINE_BIND_POINT_GRAPHICS;

    const std::uint32_t ProgramIndex = static_cast<std::uint32_t>(Programs.size());

    Programs.push_back(Held);

    // 📝 🔴 `06` §7's diagnostic-name gate, and named here rather than inside `ReachLayout` because the ordinal
    //    a layout is named by is the program's — the layout is constructed before the program stands and has no
    //    ordinal of its own until this point. The refusals are discarded for `ByteSpace`'s reason.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_PIPELINE,
                        reinterpret_cast<std::uint64_t>(Constructed),
                        "ProgramIndex graphics program",
                        ProgramIndex));

    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        reinterpret_cast<std::uint64_t>(ReachedLayout),
                        "ProgramIndex graphics reach",
                        ProgramIndex));

    return Deliver<std::uint32_t>::Result(ProgramIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE COMPUTE ROUTE
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ProgramIndex::DeclareCompute(const ComputeDeclaration& Declaring)
{
    if (DeviceEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "no device was taken" });

    const Deliver<VkPipelineShaderStageCreateInfo> Reading =
        ModuleEdge->Stage(Declaring.ModuleIndex, VK_SHADER_STAGE_COMPUTE_BIT, Declaring.Fixed);

    if (!Reading.Resolved)
        return Deliver<std::uint32_t>::Refuse(Reading.Error);

    const Deliver<VkPipelineLayout> Reached =
        ReachLayout(Declaring.LayoutIndexs, Declaring.ConstantBytes, VK_SHADER_STAGE_COMPUTE_BIT);

    if (!Reached.Resolved)
        return Deliver<std::uint32_t>::Refuse(Reached.Error);

    const VkPipelineLayout ReachedLayout = Reached.Resolve();

    VkComputePipelineCreateInfo Declaration = {};
    Declaration.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    Declaration.stage                       = Reading.Resolve();
    Declaration.layout                      = ReachedLayout;

    VkPipeline Constructed = VK_NULL_HANDLE;

    if (vkCreateComputePipelines(DeviceEdge->ActiveDevice(), VK_NULL_HANDLE, 1u, &Declaration, nullptr, &Constructed)
        != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(DeviceEdge->ActiveDevice(), ReachedLayout, nullptr);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::HostDenied, "the device rejected the compute program" });
    }

    HeldProgram Held;
    Held.Constructed   = Constructed;
    Held.ReachedLayout = ReachedLayout;
    Held.RecordedAs    = VK_PIPELINE_BIND_POINT_COMPUTE;

    const std::uint32_t ProgramIndex = static_cast<std::uint32_t>(Programs.size());

    Programs.push_back(Held);

    // 📝 🔴 `06` §7's gate, as the graphics route names its two. The ordinal runs across both routes because
    //    `Resolve` addresses one run, so a compute program and a graphics one never share one.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_PIPELINE,
                        reinterpret_cast<std::uint64_t>(Constructed),
                        "ProgramIndex compute program",
                        ProgramIndex));

    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        reinterpret_cast<std::uint64_t>(ReachedLayout),
                        "ProgramIndex compute reach",
                        ProgramIndex));

    return Deliver<std::uint32_t>::Result(ProgramIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ConstructedProgram> ProgramIndex::Resolve(std::uint32_t ProgramIndex) const
{
    if (static_cast<std::size_t>(ProgramIndex) >= Programs.size())
    {
        return Deliver<ConstructedProgram>::Refuse(
            { RefusalReason::ContentUnsupported, "no program stands at that ordinal" });
    }

    const HeldProgram& Held = Programs[ProgramIndex];

    ConstructedProgram Resolved;
    Resolved.Constructed   = Held.Constructed;
    Resolved.ReachedLayout = Held.ReachedLayout;
    Resolved.RecordedAs    = Held.RecordedAs;

    return Deliver<ConstructedProgram>::Result(Resolved);
}

std::uint32_t ProgramIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Programs.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void ProgramIndex::Reclaim()
{
    if (DeviceEdge == nullptr || DeviceEdge->ActiveDevice() == VK_NULL_HANDLE)
    {
        Programs.clear();
        return;
    }

    // 📝 The program before its layout, for each in turn. The vendor permits the layout to be destroyed while a
    //    program constructed against it stands, but the reverse ordering is what a reader expects and costs
    //    nothing to hold to.
    for (HeldProgram& Held : Programs)
    {
        if (Held.Constructed != VK_NULL_HANDLE)
            vkDestroyPipeline(DeviceEdge->ActiveDevice(), Held.Constructed, nullptr);

        if (Held.ReachedLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(DeviceEdge->ActiveDevice(), Held.ReachedLayout, nullptr);
    }

    Programs.clear();
}

ProgramIndex::~ProgramIndex()
{
    Reclaim();
}

}   // namespace Slate
