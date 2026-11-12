/*============================================================================================================================================
                                                        UVWORKSPACERASTERIZER.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceRasterizer driving Vulkan .slang MRT pipelines, O(1) GPU selection, and 3D/2D GPU picking

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

struct alignas(16) GPUSurfaceVertex
{
    float    PosAndU[4];
    float    NormalAndV[4];
    float    AlbedoAndMetal[4];
    float    ShadingParams[4];
    uint32_t NetworkPickIDs[4];
    float    Barycentric[4];
};

struct alignas(16) GPUUVCanvasVertex
{
    float    UVAndBaryXY[4];
    float    BaryZAndStats[4];
    uint32_t NetworkPickIDs[4];
};

struct alignas(16) GPUViewport3DUniforms
{
    float    ViewProjection[4][4];
    float    CameraPosAndMode[4];
    float    CameraRightAndWire[4];
    float    CameraUpAndSeam[4];
    float    CameraForwardAndLayer[4];
    uint32_t PickHighlightIDs[4];
};

struct alignas(16) GPUCanvas2DUniforms
{
    float    CanvasMinMaxUV[4];
    uint32_t OverlayAndPick[4];
};

struct alignas(16) GPUGizmoUniforms
{
    float    ViewProjection[4][4];
    uint32_t HoverAndSpace[4];
};

struct alignas(16) GPUSelectionParams
{
    uint32_t FaceCount;
    uint32_t OperationCode;
    uint32_t TargetMask;
    uint32_t AdditiveMask;
};

struct alignas(16) GPUFaceMetricInput
{
    float Pos0AndU0[4];
    float Pos1AndV0[4];
    float Pos2AndU1[4];
    float UVRest[4];
    float UVBounds[4];
};

struct alignas(16) GPUFaceMetricOutput
{
    float    L2Stretch;
    float    SignedAreaUV;
    uint32_t DistortionMask;
    uint32_t UDIMNumber;
};

struct alignas(16) GPUMetricDispatchParams
{
    uint32_t FaceCount;
    float    ReferenceScale;
    uint32_t CheckOverlaps;
    float    MaxFaceSpanU;
};

void ComputeOrbitBasis(
    const CameraOrbitRig3D& Cam,
    Mathematics::Axis3D&      OutEye,
    Mathematics::Axis3D&      OutRight,
    Mathematics::Axis3D&      OutUp,
    Mathematics::Axis3D&      OutForward) noexcept
{
    const float CosP = std::cos(Cam.PitchRadians);
    const float SinP = std::sin(Cam.PitchRadians);
    const float CosY = std::cos(Cam.YawRadians);
    const float SinY = std::sin(Cam.YawRadians);

    const Mathematics::Axis3D Offset {
        Cam.DistanceMeters * CosP * SinY,
        Cam.DistanceMeters * SinP,
        Cam.DistanceMeters * CosP * CosY,
    };
    OutEye     = Cam.TargetPivot + Offset;
    OutForward = Mathematics::NormalizeAxis(Cam.TargetPivot - OutEye, { 0.0f, 0.0f, -1.0f });
    OutRight   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutForward, { 0.0f, 1.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
    OutUp      = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutRight, OutForward), { 0.0f, 1.0f, 0.0f });
}

// Row-major Vulkan clip matrix (Y-flipped for Vulkan NDC, Z in [0, 1])
void BuildRowMajorViewProjection(
    const CameraOrbitRig3D& Cam,
    float                     AspectRatio,
    float                     OutVP[4][4]) noexcept
{
    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Cam, Eye, Right, Up, Fwd);

    const float TanHalfFov = std::tan(Cam.VerticalFovDeg * Constants::DegToRadFloat * 0.5f);
    const float ScaleX     = 1.0f / std::max(AspectRatio * TanHalfFov, 1.0e-5f);
    const float ScaleY     = -1.0f / std::max(TanHalfFov, 1.0e-5f); // Flip Y for Vulkan top-down clip space
    const float NearZ      = 0.08f;
    const float FarZ       = 60.0f;
    const float ZRange     = FarZ / (NearZ - FarZ);

    // View matrix rows: R, U, -F
    const float V[4][4] = {
        { Right.X, Right.Y, Right.Z, -Mathematics::EvaluateDot(Right, Eye) },
        { Up.X,    Up.Y,    Up.Z,    -Mathematics::EvaluateDot(Up, Eye)    },
        { -Fwd.X,  -Fwd.Y,  -Fwd.Z,   Mathematics::EvaluateDot(Fwd, Eye)   },
        { 0.0f,    0.0f,    0.0f,     1.0f                                 },
    };
    const float P[4][4] = {
        { ScaleX, 0.0f,   0.0f,    0.0f            },
        { 0.0f,   ScaleY, 0.0f,    0.0f            },
        { 0.0f,   0.0f,   ZRange,  NearZ * ZRange  },
        { 0.0f,   0.0f,  -1.0f,    0.0f            },
    };

    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            OutVP[Row][Col] =
                P[Row][0] * V[0][Col] +
                P[Row][1] * V[1][Col] +
                P[Row][2] * V[2][Col] +
                P[Row][3] * V[3][Col];
        }
    }
}

void BuildRowMajorOrtho2D(
    Mathematics::UVAxes MinUV,
    Mathematics::UVAxes MaxUV,
    float               OutOrtho[4][4]) noexcept
{
    const float SpanU = std::max(MaxUV.U - MinUV.U, 1.0e-5f);
    const float SpanV = std::max(MaxUV.V - MinUV.V, 1.0e-5f);
    std::memset(OutOrtho, 0, sizeof(float) * 16u);
    OutOrtho[0][0] =  2.0f / SpanU;
    OutOrtho[0][3] = -1.0f - (2.0f * MinUV.U / SpanU);
    OutOrtho[1][1] = -2.0f / SpanV;
    OutOrtho[1][3] =  1.0f + (2.0f * MinUV.V / SpanV);
    OutOrtho[2][2] =  0.1f;
    OutOrtho[2][3] =  0.5f;
    OutOrtho[3][3] =  1.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

UVWorkspaceRasterizer::~UVWorkspaceRasterizer()
{
    TeardownRasterizer();
}

bool UVWorkspaceRasterizer::InitializeRasterizer(
    VulkanInterchange*         BoundDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownRasterizer();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    DeviceInterchange = BoundDevice;
    if (!ComputeEncoder.InitializeEncoder(DeviceInterchange, 256u) ||
        !TransferRegister.InitializeRegister(DeviceInterchange, 16ull * 1024ull * 1024ull))
    {
        TeardownRasterizer();
        return false;
    }

    if (!PickQuery.InitializePickQuery(
            DeviceInterchange,
            &ComputeEncoder,
            &TransferRegister,
            CompiledShaderDirectory / "ViewportPickReduction.comp.spv"))
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorType, 3u> SelSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    SelectionComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "SelectionMaskCompute.comp.spv", SelSlots);

    const std::array<VkDescriptorType, 4u> StretchSlots {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    StretchComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "UVDistortionInspection.comp.spv", StretchSlots);

    if (SelectionComputeProgram == UINT32_MAX || StretchComputeProgram == UINT32_MAX)
    {
        TeardownRasterizer();
        return false;
    }

    // Create Dual-Attachment MRT + DepthSpec RenderPass (Attachment 0: RGBA8, Attachment 1: R32G32B32A32_UINT, Attachment 2: D32_SFLOAT)
    std::array<VkAttachmentDescription, 3u> Attachments {};
    Attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_R32G32B32A32_UINT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

    VkRenderPassCreateInfo TargetSpec {};
    TargetSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    TargetSpec.attachmentCount = static_cast<uint32_t>(Attachments.size());
    TargetSpec.pAttachments    = Attachments.data();
    TargetSpec.subpassCount    = 1u;
    TargetSpec.pSubpasses      = &Subpass;

    if (vkCreateRenderPass(DeviceInterchange->AccessDevice(), &TargetSpec, nullptr, &DualTargetAttachmentFormat) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 2u> GfxAllocatorCapacities {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256u },
    }};
    VkDescriptorPoolCreateInfo AllocatorSpec {};
    AllocatorSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    AllocatorSpec.maxSets       = 128u;
    AllocatorSpec.poolSizeCount = static_cast<uint32_t>(GfxAllocatorCapacities.size());
    AllocatorSpec.pPoolSizes    = GfxAllocatorCapacities.data();
    if (vkCreateDescriptorPool(DeviceInterchange->AccessDevice(), &AllocatorSpec, nullptr, &GraphicsDescriptorAllocator) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorType, 3u> SurfaceSetTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::array<VkDescriptorType, 2u> GizmoSetTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    if (!ConstructGraphicsProgram(
            CompiledShaderDirectory / "SurfaceShading.vert.spv",
            CompiledShaderDirectory / "SurfaceShading.frag.spv",
            SurfaceSetTypes,
            true,
            SurfaceProgram) ||
        !ConstructGraphicsProgram(
            CompiledShaderDirectory / "UVTileRasterization.vert.spv",
            CompiledShaderDirectory / "UVTileRasterization.frag.spv",
            SurfaceSetTypes,
            false,
            UVCanvasProgram) ||
        !ConstructGraphicsProgram(
            CompiledShaderDirectory / "GizmoRasterization.vert.spv",
            CompiledShaderDirectory / "GizmoRasterization.frag.spv",
            GizmoSetTypes,
            false,
            GizmoDepthOffProgram))
    {
        TeardownRasterizer();
        return false;
    }

    Viewport3DUniformBuffer = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUViewport3DUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Canvas2DUniformBuffer   = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUCanvas2DUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Gizmo3DUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUGizmoUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Gizmo2DUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUGizmoUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    SelectionUniformBuffer  = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUSelectionParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    StretchUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUMetricDispatchParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    StretchCountersBuffer   = ComputeEncoder.AllocateHostWritableBuffer(16u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    return true;
}

void UVWorkspaceRasterizer::TeardownRasterizer() noexcept
{
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
        vkDeviceWaitIdle(DeviceHandle);

        ReleaseFramebufferTarget(Target3D);
        ReleaseFramebufferTarget(Target2D);

        DestroyGraphicsProgram(SurfaceProgram);
        DestroyGraphicsProgram(UVCanvasProgram);
        DestroyGraphicsProgram(GizmoDepthOffProgram);

        DeviceInterchange->ReleaseBuffer(SurfaceVertexBuffer);
        DeviceInterchange->ReleaseBuffer(UVCanvasVertexBuffer);
        DeviceInterchange->ReleaseBuffer(Gizmo3DVertexBuffer);
        DeviceInterchange->ReleaseBuffer(Gizmo2DVertexBuffer);
        DeviceInterchange->ReleaseBuffer(FacePackedAttrBuffer);
        DeviceInterchange->ReleaseBuffer(SelectionBitmaskBuffer);
        DeviceInterchange->ReleaseBuffer(Viewport3DUniformBuffer);
        DeviceInterchange->ReleaseBuffer(Canvas2DUniformBuffer);
        DeviceInterchange->ReleaseBuffer(Gizmo3DUniformBuffer);
        DeviceInterchange->ReleaseBuffer(Gizmo2DUniformBuffer);
        DeviceInterchange->ReleaseBuffer(SelectionUniformBuffer);
        DeviceInterchange->ReleaseBuffer(StretchUniformBuffer);
        DeviceInterchange->ReleaseBuffer(StretchInputBuffer);
        DeviceInterchange->ReleaseBuffer(StretchOutputBuffer);
        DeviceInterchange->ReleaseBuffer(StretchCountersBuffer);

        if (GraphicsDescriptorAllocator != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(DeviceHandle, GraphicsDescriptorAllocator, nullptr);
            GraphicsDescriptorAllocator = VK_NULL_HANDLE;
        }
        if (DualTargetAttachmentFormat != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(DeviceHandle, DualTargetAttachmentFormat, nullptr);
            DualTargetAttachmentFormat = VK_NULL_HANDLE;
        }
    }

    PickQuery.TeardownPickQuery();
    TransferRegister.TeardownRegister();
    ComputeEncoder.TeardownEncoder();
    DeviceInterchange = nullptr;
}

bool UVWorkspaceRasterizer::ConstructGraphicsProgram(
    const std::filesystem::path&      VertSpirv,
    const std::filesystem::path&      FragSpirv,
    std::span<const VkDescriptorType> SlotCategories,
    bool                              DepthTestAndWrite,
    GraphicsProgramBundle&           OutBundle)
{
    VkDevice DeviceHandle         = DeviceInterchange->AccessDevice();
    OutBundle.VertBinary = DeviceInterchange->LoadShaderBinary(VertSpirv);
    OutBundle.FragBinary = DeviceInterchange->LoadShaderBinary(FragSpirv);
    if (OutBundle.VertBinary == VK_NULL_HANDLE || OutBundle.FragBinary == VK_NULL_HANDLE)
    {
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Slots(SlotCategories.size());
    for (std::size_t Idx = 0u; Idx < SlotCategories.size(); ++Idx)
    {
        Slots[Idx].binding         = static_cast<uint32_t>(Idx);
        Slots[Idx].descriptorType  = SlotCategories[Idx];
        Slots[Idx].descriptorCount = 1u;
        Slots[Idx].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SlotSpec {};
    SlotSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SlotSpec.bindingCount = static_cast<uint32_t>(Slots.size());
    SlotSpec.pBindings    = Slots.data();
    if (vkCreateDescriptorSetLayout(DeviceHandle, &SlotSpec, nullptr, &OutBundle.SlotLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo LayoutSpec {};
    LayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutSpec.setLayoutCount = 1u;
    LayoutSpec.pSetLayouts    = &OutBundle.SlotLayout;
    if (vkCreatePipelineLayout(DeviceHandle, &LayoutSpec, nullptr, &OutBundle.ProgramLayout) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> ShaderUnits {};
    ShaderUnits[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderUnits[0].module = OutBundle.VertBinary;
    ShaderUnits[0].pName  = "VertexEntry";
    ShaderUnits[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderUnits[1].module = OutBundle.FragBinary;
    ShaderUnits[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo   VertexInput {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo PrimitiveInput {};
    PrimitiveInput.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    PrimitiveInput.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportScissorConfig {};
    ViewportScissorConfig.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportScissorConfig.viewportCount = 1u;
    ViewportScissorConfig.scissorCount  = 1u;

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
    DepthStencil.depthTestEnable  = DepthTestAndWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthWriteEnable = DepthTestAndWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    BlendAtts[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[0].blendEnable    = VK_FALSE;
    BlendAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[1].blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo ColorBlend {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = static_cast<uint32_t>(BlendAtts.size());
    ColorBlend.pAttachments    = BlendAtts.data();

    const std::array<VkDynamicState, 2u> DynamicDimensions {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynSpec {};
    DynSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynSpec.dynamicStateCount = static_cast<uint32_t>(DynamicDimensions.size());
    DynSpec.pDynamicStates    = DynamicDimensions.data();

    VkGraphicsPipelineCreateInfo ProgramSpec {};
    ProgramSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ProgramSpec.stageCount          = static_cast<uint32_t>(ShaderUnits.size());
    ProgramSpec.pStages             = ShaderUnits.data();
    ProgramSpec.pVertexInputState   = &VertexInput;
    ProgramSpec.pInputAssemblyState = &PrimitiveInput;
    ProgramSpec.pViewportState      = &ViewportScissorConfig;
    ProgramSpec.pRasterizationState = &RastSpec;
    ProgramSpec.pMultisampleState   = &Multisample;
    ProgramSpec.pDepthStencilState  = &DepthStencil;
    ProgramSpec.pColorBlendState    = &ColorBlend;
    ProgramSpec.pDynamicState       = &DynSpec;
    ProgramSpec.layout              = OutBundle.ProgramLayout;
    ProgramSpec.renderPass          = DualTargetAttachmentFormat;
    ProgramSpec.subpass             = 0u;

    return vkCreateGraphicsPipelines(DeviceHandle, VK_NULL_HANDLE, 1u, &ProgramSpec, nullptr, &OutBundle.ProgramHandle) == VK_SUCCESS;
}

void UVWorkspaceRasterizer::DestroyGraphicsProgram(GraphicsProgramBundle& Bundle) noexcept
{
    if (DeviceInterchange == nullptr || !DeviceInterchange->Ready())
    {
        return;
    }
    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();
    if (Bundle.ProgramHandle != VK_NULL_HANDLE)       { vkDestroyPipeline(DeviceHandle, Bundle.ProgramHandle, nullptr); Bundle.ProgramHandle = VK_NULL_HANDLE; }
    if (Bundle.ProgramLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(DeviceHandle, Bundle.ProgramLayout, nullptr); Bundle.ProgramLayout = VK_NULL_HANDLE; }
    if (Bundle.SlotLayout != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(DeviceHandle, Bundle.SlotLayout, nullptr); Bundle.SlotLayout = VK_NULL_HANDLE; }
    if (Bundle.VertBinary != VK_NULL_HANDLE)     { vkDestroyShaderModule(DeviceHandle, Bundle.VertBinary, nullptr); Bundle.VertBinary = VK_NULL_HANDLE; }
    if (Bundle.FragBinary != VK_NULL_HANDLE)     { vkDestroyShaderModule(DeviceHandle, Bundle.FragBinary, nullptr); Bundle.FragBinary = VK_NULL_HANDLE; }
}

bool UVWorkspaceRasterizer::EnsureFramebufferTarget(MRTFramebufferTarget& Target, uint32_t WidthPx, uint32_t HeightPx)
{
    if (Target.Framebuffer != VK_NULL_HANDLE && Target.WidthPx == WidthPx && Target.HeightPx == HeightPx)
    {
        return true;
    }

    ReleaseFramebufferTarget(Target);

    Target.ColorImage = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    Target.PickImage = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    Target.DepthImage = DeviceInterchange->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    if (Target.ColorImage.ViewHandle == VK_NULL_HANDLE ||
        Target.PickImage.ViewHandle == VK_NULL_HANDLE ||
        Target.DepthImage.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseFramebufferTarget(Target);
        return false;
    }

    const std::array<VkImageView, 3u> Views {{
        Target.ColorImage.ViewHandle,
        Target.PickImage.ViewHandle,
        Target.DepthImage.ViewHandle,
    }};

    VkFramebufferCreateInfo FbSpec {};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = DualTargetAttachmentFormat;
    FbSpec.attachmentCount = static_cast<uint32_t>(Views.size());
    FbSpec.pAttachments    = Views.data();
    FbSpec.width           = WidthPx;
    FbSpec.height          = HeightPx;
    FbSpec.layers          = 1u;

    if (vkCreateFramebuffer(DeviceInterchange->AccessDevice(), &FbSpec, nullptr, &Target.Framebuffer) != VK_SUCCESS)
    {
        ReleaseFramebufferTarget(Target);
        return false;
    }

    Target.WidthPx    = WidthPx;
    Target.HeightPx   = HeightPx;
    Target.PickLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void UVWorkspaceRasterizer::ReleaseFramebufferTarget(MRTFramebufferTarget& Target) noexcept
{
    if (DeviceInterchange != nullptr && DeviceInterchange->Ready())
    {
        if (Target.Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(DeviceInterchange->AccessDevice(), Target.Framebuffer, nullptr);
            Target.Framebuffer = VK_NULL_HANDLE;
        }
        DeviceInterchange->ReleaseImage2D(Target.ColorImage);
        DeviceInterchange->ReleaseImage2D(Target.PickImage);
        DeviceInterchange->ReleaseImage2D(Target.DepthImage);
    }
    Target.WidthPx  = 0u;
    Target.HeightPx = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      POLYGONNETWORK UPLOAD & GPU COMPUTE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceRasterizer::UploadPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             InstanceSlot)
{
    if (DeviceInterchange == nullptr || Network.Faces.empty())
    {
        return false;
    }

    UploadedFaceCount = static_cast<uint32_t>(Network.Faces.size());
    if (CachedFaceStretch.size() != UploadedFaceCount)
    {
        CachedFaceStretch.assign(UploadedFaceCount, 1.0f);
    }

    // 1. Pack FacePackedAttributes & SelectionBitmask
    std::vector<uint32_t> PackedFaceAttrs(UploadedFaceCount);
    for (uint32_t FaceIdx = 0u; FaceIdx < UploadedFaceCount; ++FaceIdx)
    {
        const uint32_t LayerOrd = Network.Faces[FaceIdx].PriorityLayerOrdinal;
        const uint32_t PartOrd  = Network.Faces[FaceIdx].PartGroupOrdinal;
        PackedFaceAttrs[FaceIdx] = (LayerOrd & 0xFFFFu) | ((PartOrd & 0xFFFFu) << 16u);
    }

    const uint32_t BitmaskWordCount = (UploadedFaceCount + 31u) / 32u;
    DeviceInterchange->ReleaseBuffer(FacePackedAttrBuffer);
    FacePackedAttrBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        PackedFaceAttrs.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(FacePackedAttrBuffer, PackedFaceAttrs);

    if (SelectionBitmaskBuffer.ByteCapacity < BitmaskWordCount * sizeof(uint32_t))
    {
        DeviceInterchange->ReleaseBuffer(SelectionBitmaskBuffer);
        SelectionBitmaskBuffer = ComputeEncoder.AllocateHostWritableBuffer(
            std::max<uint64_t>(16ull, BitmaskWordCount * sizeof(uint32_t)),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::vector<uint32_t> ZeroWords(std::max(4u, BitmaskWordCount), 0u);
        ComputeEncoder.UploadSpanToBuffer<uint32_t>(SelectionBitmaskBuffer, ZeroWords);
    }

    // 2. Build 3D SurfaceVertexBuffer (starting with 2 ground-grid triangles at NetworkPickIDs.x = 0)
    std::vector<GPUSurfaceVertex> Verts3D;
    Verts3D.reserve(6u + UploadedFaceCount * 6u);

    constexpr float GridHalf = 4.2f;
    const std::array<Mathematics::Axis3D, 6u> GridQuad {{
        { -GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f,  GridHalf },
        { -GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f,  GridHalf }, { -GridHalf, -0.02f,  GridHalf },
    }};
    for (const Mathematics::Axis3D& GP : GridQuad)
    {
        GPUSurfaceVertex GV {};
        GV.PosAndU[0]        = GP.X; GV.PosAndU[1] = GP.Y; GV.PosAndU[2] = GP.Z;
        GV.NormalAndV[1]     = 1.0f;
        GV.NetworkPickIDs[0] = 0u; // Ground Grid marker
        GV.NetworkPickIDs[1] = GeometrySpace::InvalidNetworkIndex;
        Verts3D.push_back(GV);
    }
    GroundGridVertexCount = 6u;

    // 3. Build 2D UVCanvasVertexBuffer (starting with background quad at NetworkPickIDs.x = 0)
    std::vector<GPUUVCanvasVertex> Verts2D;
    Verts2D.reserve(6u + UploadedFaceCount * 6u);

    const std::array<Mathematics::UVAxes, 6u> BgQuad {{
        { -2.0f, -2.0f }, { 12.0f, -2.0f }, { 12.0f, 6.0f },
        { -2.0f, -2.0f }, { 12.0f,  6.0f }, { -2.0f, 6.0f },
    }};
    for (const Mathematics::UVAxes& BUV : BgQuad)
    {
        GPUUVCanvasVertex BV {};
        BV.UVAndBaryXY[0]    = BUV.U;
        BV.UVAndBaryXY[1]    = BUV.V;
        BV.NetworkPickIDs[0] = 0u;
        BV.NetworkPickIDs[1] = GeometrySpace::InvalidNetworkIndex;
        Verts2D.push_back(BV);
    }

    // Fan-triangulate each PolygonNetwork Face for both 3D and 2D streams
    for (uint32_t FaceIdx = 0u; FaceIdx < UploadedFaceCount; ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopEdges;
        LoopEdges.reserve(CurrentFace.CornerCount);
        uint32_t EdgeCur = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount && EdgeCur < Network.Edges.size(); ++Corner)
        {
            LoopEdges.push_back(EdgeCur);
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }
        if (LoopEdges.size() < 3u)
        {
            continue;
        }

        const float SignedUV = Network.EvaluateFaceSignedAreaUV(FaceIdx);
        const float Stretch  = FaceIdx < CachedFaceStretch.size() ? CachedFaceStretch[FaceIdx] : 1.0f;

        for (std::size_t Tri = 1u; Tri + 1u < LoopEdges.size(); ++Tri)
        {
            const std::array<uint32_t, 3u> TriEdges {{ LoopEdges[0u], LoopEdges[Tri], LoopEdges[Tri + 1u] }};
            const std::array<std::array<float, 3u>, 3u> Barys {{
                {{ 1.0f, 0.0f, 0.0f }},
                {{ 0.0f, 1.0f, 0.0f }},
                {{ 0.0f, 0.0f, 1.0f }},
            }};

            const bool Pin0  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[0u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Pin1  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[1u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Pin2  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[2u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Seam0 = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[1u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);
            const bool Seam1 = (Tri + 2u == LoopEdges.size()) &&
                               GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[2u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);
            const bool Seam2 = (Tri == 1u) &&
                               GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[0u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);

            const uint32_t Packed2DMask =
                (static_cast<uint32_t>(CurrentFace.PriorityLayerOrdinal) & 0xFFFFu) |
                (Pin0  ? (1u << 16u) : 0u) |
                (Pin1  ? (1u << 17u) : 0u) |
                (Pin2  ? (1u << 18u) : 0u) |
                (Seam0 ? (1u << 19u) : 0u) |
                (Seam1 ? (1u << 20u) : 0u) |
                (Seam2 ? (1u << 21u) : 0u);

            for (std::size_t Corner = 0u; Corner < 3u; ++Corner)
            {
                const GeometrySpace::DirectedEdge& DE = Network.Edges[TriEdges[Corner]];
                const GeometrySpace::Vertex&       VX = Network.Vertices[DE.OriginVertexIndex];
                const bool SeamBit = GeometrySpace::QueryEdgeAttributeEnabled(DE.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);

                const uint32_t SurfaceMask =
                    (static_cast<uint32_t>(CurrentFace.Shading.MatCapSlot) & 0xFFu) |
                    ((static_cast<uint32_t>(CurrentFace.PriorityLayerOrdinal) & 0xFFFFu) << 8u) |
                    (SeamBit ? (1u << 24u) : 0u);

                GPUSurfaceVertex SV {};
                SV.PosAndU[0]        = VX.Axes.X;
                SV.PosAndU[1]        = VX.Axes.Y;
                SV.PosAndU[2]        = VX.Axes.Z;
                SV.PosAndU[3]        = DE.ParameterUV.U;
                SV.NormalAndV[0]     = CurrentFace.GeometricNormal.X;
                SV.NormalAndV[1]     = CurrentFace.GeometricNormal.Y;
                SV.NormalAndV[2]     = CurrentFace.GeometricNormal.Z;
                SV.NormalAndV[3]     = DE.ParameterUV.V;
                SV.AlbedoAndMetal[0] = CurrentFace.Shading.AlbedoLinearRGB.X;
                SV.AlbedoAndMetal[1] = CurrentFace.Shading.AlbedoLinearRGB.Y;
                SV.AlbedoAndMetal[2] = CurrentFace.Shading.AlbedoLinearRGB.Z;
                SV.AlbedoAndMetal[3] = CurrentFace.Shading.Metallic;
                SV.ShadingParams[0]  = CurrentFace.Shading.Roughness;
                SV.ShadingParams[1]  = CurrentFace.Shading.Specular;
                SV.ShadingParams[2]  = CurrentFace.Shading.MatCapBlend;
                SV.ShadingParams[3]  = Stretch;
                SV.NetworkPickIDs[0] = std::max(1u, InstanceSlot);
                SV.NetworkPickIDs[1] = FaceIdx;
                SV.NetworkPickIDs[2] = TriEdges[Corner];
                SV.NetworkPickIDs[3] = SurfaceMask;
                SV.Barycentric[0]    = Barys[Corner][0];
                SV.Barycentric[1]    = Barys[Corner][1];
                SV.Barycentric[2]    = Barys[Corner][2];
                Verts3D.push_back(SV);

                GPUUVCanvasVertex UVV {};
                UVV.UVAndBaryXY[0]   = DE.ParameterUV.U;
                UVV.UVAndBaryXY[1]   = DE.ParameterUV.V;
                UVV.UVAndBaryXY[2]   = Barys[Corner][0];
                UVV.UVAndBaryXY[3]   = Barys[Corner][1];
                UVV.BaryZAndStats[0] = Barys[Corner][2];
                UVV.BaryZAndStats[1] = Stretch;
                UVV.BaryZAndStats[2] = SignedUV;
                UVV.BaryZAndStats[3] = 0.0f;
                UVV.NetworkPickIDs[0] = std::max(1u, InstanceSlot);
                UVV.NetworkPickIDs[1] = FaceIdx;
                UVV.NetworkPickIDs[2] = TriEdges[Corner];
                UVV.NetworkPickIDs[3] = Packed2DMask;
                Verts2D.push_back(UVV);
            }
        }
    }

    SurfaceVertexCount  = static_cast<uint32_t>(Verts3D.size());
    UVCanvasVertexCount = static_cast<uint32_t>(Verts2D.size());

    DeviceInterchange->ReleaseBuffer(SurfaceVertexBuffer);
    SurfaceVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        Verts3D.size() * sizeof(GPUSurfaceVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUSurfaceVertex>(SurfaceVertexBuffer, Verts3D);

    DeviceInterchange->ReleaseBuffer(UVCanvasVertexBuffer);
    UVCanvasVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        Verts2D.size() * sizeof(GPUUVCanvasVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUUVCanvasVertex>(UVCanvasVertexBuffer, Verts2D);

    return true;
}

bool UVWorkspaceRasterizer::DispatchGPUSelection(
    GPUSelectionOperation Operation,
    uint32_t              TargetMask,
    bool                  AdditiveMask)
{
    if (DeviceInterchange == nullptr || UploadedFaceCount == 0u || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    GPUSelectionParams Params {};
    Params.FaceCount     = UploadedFaceCount;
    Params.OperationCode = static_cast<uint32_t>(Operation);
    Params.TargetMask   = TargetMask;
    Params.AdditiveMask  = AdditiveMask ? 1u : 0u;

    const std::array<GPUSelectionParams, 1u> ParamsArr {{ Params }};
    ComputeEncoder.UploadSpanToBuffer<GPUSelectionParams>(SelectionUniformBuffer, ParamsArr);

    const uint32_t WordCount = (UploadedFaceCount + 31u) / 32u;
    const uint32_t Groups    = (WordCount + 63u) / 64u;

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
    const std::array<ComputeSlotDescriptor, 3u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SelectionUniformBuffer.BufferHandle, sizeof(GPUSelectionParams) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FacePackedAttrBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SelectionBitmaskBuffer.BufferHandle, VK_WHOLE_SIZE },
    }};
    (void)ComputeEncoder.EncodeComputeDispatch(Cmd, SelectionComputeProgram, Slots, std::max(1u, Groups));
    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    (void)DeviceInterchange->AwaitTimelineTick(Signal);
    vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return true;
}

void UVWorkspaceRasterizer::UploadSelectionBitmaskWords(std::span<const uint32_t> PackedWords)
{
    if (DeviceInterchange == nullptr || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE || PackedWords.empty())
    {
        return;
    }
    const std::size_t MaxWords = static_cast<std::size_t>(SelectionBitmaskBuffer.ByteCapacity / sizeof(uint32_t));
    const std::size_t Count    = std::min(PackedWords.size(), MaxWords);
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(SelectionBitmaskBuffer, PackedWords.subspan(0u, Count));
}

std::vector<uint32_t> UVWorkspaceRasterizer::ReadbackSelectionBitmaskWords()
{
    const uint32_t WordCount = (UploadedFaceCount + 31u) / 32u;
    std::vector<uint32_t> Output(WordCount, 0u);
    if (DeviceInterchange == nullptr || WordCount == 0u || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return Output;
    }

    VkCommandBuffer Cmd    = DeviceInterchange->BeginSingleTimeCommands();
    ReadbackTicket  Ticket = TransferRegister.EncodeBufferReadback(Cmd, SelectionBitmaskBuffer.BufferHandle, 0ull, WordCount * sizeof(uint32_t));
    const uint64_t  Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(Ticket, Signal);
    (void)TransferRegister.AwaitAndCopyTyped<uint32_t>(Ticket, Output);
    vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return Output;
}

GPUUVDistortionSummary UVWorkspaceRasterizer::EvaluateUVDistortionInspection(
    const GeometrySpace::PolygonNetwork& Network,
    bool                                 CheckOverlaps)
{
    GPUUVDistortionSummary Summary {};
    const uint32_t FaceCount = static_cast<uint32_t>(Network.Faces.size());
    if (DeviceInterchange == nullptr || FaceCount == 0u)
    {
        return Summary;
    }

    std::vector<GPUFaceMetricInput> Inputs(FaceCount);
    double Total3D = 0.0;
    double TotalUV = 0.0;

    for (uint32_t FaceIdx = 0u; FaceIdx < FaceCount; ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        const uint32_t E0 = CurrentFace.BoundaryLoopEdgeIndex;
        const uint32_t E1 = Network.Edges[E0].NextEdgeIndex;
        const uint32_t E2 = Network.Edges[E1].NextEdgeIndex;

        const Mathematics::Axis3D P0 = Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes;
        const Mathematics::Axis3D P1 = Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes;
        const Mathematics::Axis3D P2 = Network.Vertices[Network.Edges[E2].OriginVertexIndex].Axes;

        const Mathematics::UVAxes T0 = Network.Edges[E0].ParameterUV;
        const Mathematics::UVAxes T1 = Network.Edges[E1].ParameterUV;
        const Mathematics::UVAxes T2 = Network.Edges[E2].ParameterUV;

        float MinU = T0.U, MaxU = T0.U, MinV = T0.V, MaxV = T0.V;
        uint32_t Walk = E0;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
        {
            const Mathematics::UVAxes UV = Network.Edges[Walk].ParameterUV;
            MinU = std::min(MinU, UV.U); MaxU = std::max(MaxU, UV.U);
            MinV = std::min(MinV, UV.V); MaxV = std::max(MaxV, UV.V);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        const float Area3D = Network.EvaluateFaceArea3D(FaceIdx);
        const float AreaUV = std::abs(Network.EvaluateFaceSignedAreaUV(FaceIdx));
        Total3D += Area3D;
        TotalUV += AreaUV;

        GPUFaceMetricInput& In = Inputs[FaceIdx];
        In.Pos0AndU0[0] = P0.X; In.Pos0AndU0[1] = P0.Y; In.Pos0AndU0[2] = P0.Z; In.Pos0AndU0[3] = T0.U;
        In.Pos1AndV0[0] = P1.X; In.Pos1AndV0[1] = P1.Y; In.Pos1AndV0[2] = P1.Z; In.Pos1AndV0[3] = T0.V;
        In.Pos2AndU1[0] = P2.X; In.Pos2AndU1[1] = P2.Y; In.Pos2AndU1[2] = P2.Z; In.Pos2AndU1[3] = T1.U;
        In.UVRest[0]    = T1.V; In.UVRest[1]    = T2.U; In.UVRest[2]    = T2.V; In.UVRest[3]    = Area3D;
        In.UVBounds[0]  = MinU; In.UVBounds[1]  = MinV; In.UVBounds[2]  = MaxU; In.UVBounds[3]  = MaxV;
    }

    const float RefScale = Total3D > 1.0e-8 ? static_cast<float>(std::sqrt(TotalUV / Total3D)) : 1.0f;

    DeviceInterchange->ReleaseBuffer(StretchInputBuffer);
    DeviceInterchange->ReleaseBuffer(StretchOutputBuffer);
    StretchInputBuffer  = ComputeEncoder.AllocateHostWritableBuffer(Inputs.size() * sizeof(GPUFaceMetricInput), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    StretchOutputBuffer = ComputeEncoder.AllocateHostWritableBuffer(FaceCount * sizeof(GPUFaceMetricOutput), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUFaceMetricInput>(StretchInputBuffer, Inputs);

    GPUMetricDispatchParams Params { FaceCount, RefScale, CheckOverlaps ? 1u : 0u, 0u };
    const std::array<GPUMetricDispatchParams, 1u> ParamsSpan {{ Params }};
    ComputeEncoder.UploadSpanToBuffer<GPUMetricDispatchParams>(StretchUniformBuffer, ParamsSpan);

    const std::array<uint32_t, 4u> ZeroCounters {{ 0u, 0u, 0u, 0u }};
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(StretchCountersBuffer, ZeroCounters);

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();
    const std::array<ComputeSlotDescriptor, 4u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, StretchUniformBuffer.BufferHandle, sizeof(GPUMetricDispatchParams) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchInputBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchOutputBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchCountersBuffer.BufferHandle, 16u },
    }};
    (void)ComputeEncoder.EncodeComputeDispatch(Cmd, StretchComputeProgram, Slots, (FaceCount + 63u) / 64u);

    ReadbackTicket CountsTicket  = TransferRegister.EncodeBufferReadback(Cmd, StretchCountersBuffer.BufferHandle, 0ull, 12ull);
    ReadbackTicket OutputsTicket = TransferRegister.EncodeBufferReadback(Cmd, StretchOutputBuffer.BufferHandle, 0ull, FaceCount * sizeof(GPUFaceMetricOutput));
    const uint64_t Signal        = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(CountsTicket, Signal);
    TransferRegister.BindTicketTimelineSignal(OutputsTicket, Signal);

    std::array<uint32_t, 3u> RawCounts {{ 0u, 0u, 0u }};
    std::vector<GPUFaceMetricOutput> Outputs(FaceCount);
    (void)TransferRegister.AwaitAndCopyTyped<uint32_t>(CountsTicket, RawCounts);
    (void)TransferRegister.AwaitAndCopyTyped<GPUFaceMetricOutput>(OutputsTicket, Outputs);
    vkFreeCommandBuffers(DeviceInterchange->AccessDevice(), DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);

    Summary.FlippedFaceCount = RawCounts[0u];
    Summary.OverlapFaceCount = RawCounts[1u];
    Summary.MeanL2Stretch    = static_cast<float>(RawCounts[2u]) / (1000.0f * static_cast<float>(FaceCount));
    Summary.PerFaceStretch.resize(FaceCount);
    for (uint32_t Idx = 0u; Idx < FaceCount; ++Idx)
    {
        Summary.PerFaceStretch[Idx] = Outputs[Idx].L2Stretch;
    }
    CachedFaceStretch = Summary.PerFaceStretch;
    return Summary;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D & 2D VIEWPORT MRT RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceRasterizer::Rasterize3DViewport(
    uint32_t                       WidthPx,
    uint32_t                       HeightPx,
    const Viewport3DRenderOptions& Options,
    const Editor::TransformGizmo*  OptionalGizmo3D,
    std::vector<uint8_t>*          OptionalOutRGBA)
{
    if (DeviceInterchange == nullptr || SurfaceVertexCount == 0u || !EnsureFramebufferTarget(Target3D, WidthPx, HeightPx))
    {
        return false;
    }

    vkResetDescriptorPool(DeviceInterchange->AccessDevice(), GraphicsDescriptorAllocator, 0u);

    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Options.Camera, Eye, Right, Up, Fwd);

    GPUViewport3DUniforms Uniforms {};
    BuildRowMajorViewProjection(Options.Camera, static_cast<float>(WidthPx) / static_cast<float>(HeightPx), Uniforms.ViewProjection);
    Uniforms.CameraPosAndMode[0]      = Eye.X;
    Uniforms.CameraPosAndMode[1]      = Eye.Y;
    Uniforms.CameraPosAndMode[2]      = Eye.Z;
    Uniforms.CameraPosAndMode[3]      = static_cast<float>(static_cast<uint32_t>(Options.DisplayMode));
    Uniforms.CameraRightAndWire[0]    = Right.X;
    Uniforms.CameraRightAndWire[1]    = Right.Y;
    Uniforms.CameraRightAndWire[2]    = Right.Z;
    Uniforms.CameraRightAndWire[3]    = Options.WireframeEnabled ? 1.0f : 0.0f;
    Uniforms.CameraUpAndSeam[0]       = Up.X;
    Uniforms.CameraUpAndSeam[1]       = Up.Y;
    Uniforms.CameraUpAndSeam[2]       = Up.Z;
    Uniforms.CameraUpAndSeam[3]       = Options.SeamOverlayEnabled ? 1.0f : 0.0f;
    Uniforms.CameraForwardAndLayer[0] = Fwd.X;
    Uniforms.CameraForwardAndLayer[1] = Fwd.Y;
    Uniforms.CameraForwardAndLayer[2] = Fwd.Z;
    Uniforms.CameraForwardAndLayer[3] = static_cast<float>(Options.ActiveLayerOrdinal);
    Uniforms.PickHighlightIDs[0]      = Options.HoveredFaceIndex;

    const std::array<GPUViewport3DUniforms, 1u> UniSpan {{ Uniforms }};
    ComputeEncoder.UploadSpanToBuffer<GPUViewport3DUniforms>(Viewport3DUniformBuffer, UniSpan);

    uint32_t GizmoVertCount = 0u;
    if (Options.RenderGizmo && OptionalGizmo3D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> GizmoVerts = OptionalGizmo3D->BuildTriangleStream(Right, Up, Fwd);
        GizmoVertCount = static_cast<uint32_t>(GizmoVerts.size());
        if (GizmoVertCount > 0u)
        {
            const uint64_t ReqBytes = GizmoVerts.size() * sizeof(Editor::GizmoGPUVertex);
            if (Gizmo3DVertexBuffer.ByteCapacity < ReqBytes)
            {
                DeviceInterchange->ReleaseBuffer(Gizmo3DVertexBuffer);
                Gizmo3DVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(ReqBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            ComputeEncoder.UploadSpanToBuffer<Editor::GizmoGPUVertex>(Gizmo3DVertexBuffer, GizmoVerts);

            GPUGizmoUniforms GizUni {};
            std::memcpy(GizUni.ViewProjection, Uniforms.ViewProjection, sizeof(Uniforms.ViewProjection));
            GizUni.HoverAndSpace[0] = static_cast<uint32_t>(OptionalGizmo3D->QueryHoveredHandle());
            GizUni.HoverAndSpace[1] = static_cast<uint32_t>(OptionalGizmo3D->QueryActiveHandle());
            GizUni.HoverAndSpace[2] = 0u;
            const std::array<GPUGizmoUniforms, 1u> GizSpan {{ GizUni }};
            ComputeEncoder.UploadSpanToBuffer<GPUGizmoUniforms>(Gizmo3DUniformBuffer, GizSpan);
        }
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    // Allocate descriptor sets
    VkDescriptorSet SurfaceSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo SurfAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &SurfaceProgram.SlotLayout };
    if (vkAllocateDescriptorSets(DeviceHandle, &SurfAlloc, &SurfaceSet) != VK_SUCCESS || SurfaceSet == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<VkDescriptorBufferInfo, 3u> SurfBufs {{
        { Viewport3DUniformBuffer.BufferHandle, 0u, sizeof(GPUViewport3DUniforms) },
        { SurfaceVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        { SelectionBitmaskBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 3u> SurfWrites {};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        SurfWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        SurfWrites[Idx].dstSet          = SurfaceSet;
        SurfWrites[Idx].dstBinding      = Idx;
        SurfWrites[Idx].descriptorCount = 1u;
        SurfWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SurfWrites[Idx].pBufferInfo     = &SurfBufs[Idx];
    }
    vkUpdateDescriptorSets(DeviceHandle, 3u, SurfWrites.data(), 0u, nullptr);

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (GizmoVertCount > 0u)
    {
        VkDescriptorSetAllocateInfo GizAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &GizmoDepthOffProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &GizAlloc, &GizmoSet) != VK_SUCCESS || GizmoSet == VK_NULL_HANDLE)
        {
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 2u> GizBufs {{
            { Gizmo3DUniformBuffer.BufferHandle, 0u, sizeof(GPUGizmoUniforms) },
            { Gizmo3DVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        }};
        std::array<VkWriteDescriptorSet, 2u> GizWrites {};
        for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
        {
            GizWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            GizWrites[Idx].dstSet          = GizmoSet;
            GizWrites[Idx].dstBinding      = Idx;
            GizWrites[Idx].descriptorCount = 1u;
            GizWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            GizWrites[Idx].pBufferInfo     = &GizBufs[Idx];
        }
        vkUpdateDescriptorSets(DeviceHandle, 2u, GizWrites.data(), 0u, nullptr);
    }

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();

    std::array<VkClearValue, 3u> Clears {};
    Clears[0].color        = { { 0.068f, 0.072f, 0.084f, 1.0f } };
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[2] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo TargetBegin {};
    TargetBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    TargetBegin.renderPass        = DualTargetAttachmentFormat;
    TargetBegin.framebuffer       = Target3D.Framebuffer;
    TargetBegin.renderArea.extent = { WidthPx, HeightPx };
    TargetBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    TargetBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &TargetBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport VP { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SurfaceProgram.ProgramHandle);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SurfaceProgram.ProgramLayout, 0u, 1u, &SurfaceSet, 0u, nullptr);
    const uint32_t FirstVert = Options.GroundGridEnabled ? 0u : GroundGridVertexCount;
    vkCmdDraw(Cmd, SurfaceVertexCount - FirstVert, 1u, FirstVert, 0u);

    if (GizmoVertCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffProgram.ProgramHandle);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, GizmoVertCount, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Cmd);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    ReadbackTicket ColorTicket {};
    if (OptionalOutRGBA != nullptr)
    {
        ColorTicket = TransferRegister.EncodeImageRegionReadback(
            Cmd,
            Target3D.ColorImage.ImageHandle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0,
            0,
            WidthPx,
            HeightPx,
            4u);
    }

    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    if (OptionalOutRGBA != nullptr)
    {
        TransferRegister.BindTicketTimelineSignal(ColorTicket, Signal);
        OptionalOutRGBA->resize(static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
        (void)TransferRegister.AwaitAndCopyBytes(ColorTicket, *OptionalOutRGBA);
    }
    else
    {
        (void)DeviceInterchange->AwaitTimelineTick(Signal);
    }

    vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return true;
}

bool UVWorkspaceRasterizer::Rasterize2DUVCanvas(
    uint32_t                      WidthPx,
    uint32_t                      HeightPx,
    const Canvas2DRenderOptions&  Options,
    const Editor::TransformGizmo* OptionalGizmo2D,
    std::vector<uint8_t>*         OptionalOutRGBA)
{
    if (DeviceInterchange == nullptr || UVCanvasVertexCount == 0u || !EnsureFramebufferTarget(Target2D, WidthPx, HeightPx))
    {
        return false;
    }

    vkResetDescriptorPool(DeviceInterchange->AccessDevice(), GraphicsDescriptorAllocator, 0u);

    GPUCanvas2DUniforms Uniforms {};
    Uniforms.CanvasMinMaxUV[0] = Options.VisibleMinUV.U;
    Uniforms.CanvasMinMaxUV[1] = Options.VisibleMinUV.V;
    Uniforms.CanvasMinMaxUV[2] = Options.VisibleMaxUV.U;
    Uniforms.CanvasMinMaxUV[3] = Options.VisibleMaxUV.V;
    const uint32_t OverlayBits =
        (Options.CheckerEnabled     ? 1u : 0u) |
        (Options.SeamOverlayEnabled ? (1u << 1u) : 0u) |
        (Options.PinOverlayEnabled  ? (1u << 2u) : 0u) |
        (static_cast<uint32_t>(Options.BackdropPreset) << 8u);

    Uniforms.OverlayAndPick[0] = static_cast<uint32_t>(Options.DisplayMode);
    Uniforms.OverlayAndPick[1] = OverlayBits;
    Uniforms.OverlayAndPick[2] = Options.ActiveLayerOrdinal;
    Uniforms.OverlayAndPick[3] = Options.HoveredFaceIndex;

    const std::array<GPUCanvas2DUniforms, 1u> UniSpan {{ Uniforms }};
    ComputeEncoder.UploadSpanToBuffer<GPUCanvas2DUniforms>(Canvas2DUniformBuffer, UniSpan);

    uint32_t GizmoVertCount = 0u;
    if (Options.RenderGizmo && OptionalGizmo2D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> GizmoVerts = OptionalGizmo2D->BuildTriangleStream();
        GizmoVertCount = static_cast<uint32_t>(GizmoVerts.size());
        if (GizmoVertCount > 0u)
        {
            const uint64_t ReqBytes = GizmoVerts.size() * sizeof(Editor::GizmoGPUVertex);
            if (Gizmo2DVertexBuffer.ByteCapacity < ReqBytes)
            {
                DeviceInterchange->ReleaseBuffer(Gizmo2DVertexBuffer);
                Gizmo2DVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(ReqBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            ComputeEncoder.UploadSpanToBuffer<Editor::GizmoGPUVertex>(Gizmo2DVertexBuffer, GizmoVerts);

            GPUGizmoUniforms GizUni {};
            BuildRowMajorOrtho2D(Options.VisibleMinUV, Options.VisibleMaxUV, GizUni.ViewProjection);
            GizUni.HoverAndSpace[0] = static_cast<uint32_t>(OptionalGizmo2D->QueryHoveredHandle());
            GizUni.HoverAndSpace[1] = static_cast<uint32_t>(OptionalGizmo2D->QueryActiveHandle());
            GizUni.HoverAndSpace[2] = 0u;
            const std::array<GPUGizmoUniforms, 1u> GizSpan {{ GizUni }};
            ComputeEncoder.UploadSpanToBuffer<GPUGizmoUniforms>(Gizmo2DUniformBuffer, GizSpan);
        }
    }

    VkDevice DeviceHandle = DeviceInterchange->AccessDevice();

    VkDescriptorSet CanvasSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo CanvAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &UVCanvasProgram.SlotLayout };
    if (vkAllocateDescriptorSets(DeviceHandle, &CanvAlloc, &CanvasSet) != VK_SUCCESS || CanvasSet == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<VkDescriptorBufferInfo, 3u> CanvBufs {{
        { Canvas2DUniformBuffer.BufferHandle, 0u, sizeof(GPUCanvas2DUniforms) },
        { UVCanvasVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        { SelectionBitmaskBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 3u> CanvWrites {};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        CanvWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        CanvWrites[Idx].dstSet          = CanvasSet;
        CanvWrites[Idx].dstBinding      = Idx;
        CanvWrites[Idx].descriptorCount = 1u;
        CanvWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        CanvWrites[Idx].pBufferInfo     = &CanvBufs[Idx];
    }
    vkUpdateDescriptorSets(DeviceHandle, 3u, CanvWrites.data(), 0u, nullptr);

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (GizmoVertCount > 0u)
    {
        VkDescriptorSetAllocateInfo GizAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorAllocator, 1u, &GizmoDepthOffProgram.SlotLayout };
        if (vkAllocateDescriptorSets(DeviceHandle, &GizAlloc, &GizmoSet) != VK_SUCCESS || GizmoSet == VK_NULL_HANDLE)
        {
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 2u> GizBufs {{
            { Gizmo2DUniformBuffer.BufferHandle, 0u, sizeof(GPUGizmoUniforms) },
            { Gizmo2DVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        }};
        std::array<VkWriteDescriptorSet, 2u> GizWrites {};
        for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
        {
            GizWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            GizWrites[Idx].dstSet          = GizmoSet;
            GizWrites[Idx].dstBinding      = Idx;
            GizWrites[Idx].descriptorCount = 1u;
            GizWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            GizWrites[Idx].pBufferInfo     = &GizBufs[Idx];
        }
        vkUpdateDescriptorSets(DeviceHandle, 2u, GizWrites.data(), 0u, nullptr);
    }

    VkCommandBuffer Cmd = DeviceInterchange->BeginSingleTimeCommands();

    std::array<VkClearValue, 3u> Clears {};
    Clears[0].color           = { { 0.050f, 0.052f, 0.060f, 1.0f } };
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[2] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil    = { 1.0f, 0u };

    VkRenderPassBeginInfo TargetBegin {};
    TargetBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    TargetBegin.renderPass        = DualTargetAttachmentFormat;
    TargetBegin.framebuffer       = Target2D.Framebuffer;
    TargetBegin.renderArea.extent = { WidthPx, HeightPx };
    TargetBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    TargetBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &TargetBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport VP { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UVCanvasProgram.ProgramHandle);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UVCanvasProgram.ProgramLayout, 0u, 1u, &CanvasSet, 0u, nullptr);
    vkCmdDraw(Cmd, UVCanvasVertexCount, 1u, 0u, 0u);

    if (GizmoVertCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffProgram.ProgramHandle);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffProgram.ProgramLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, GizmoVertCount, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Cmd);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    ReadbackTicket ColorTicket {};
    if (OptionalOutRGBA != nullptr)
    {
        ColorTicket = TransferRegister.EncodeImageRegionReadback(
            Cmd,
            Target2D.ColorImage.ImageHandle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0,
            0,
            WidthPx,
            HeightPx,
            4u);
    }

    const uint64_t Signal = DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    if (OptionalOutRGBA != nullptr)
    {
        TransferRegister.BindTicketTimelineSignal(ColorTicket, Signal);
        OptionalOutRGBA->resize(static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
        (void)TransferRegister.AwaitAndCopyBytes(ColorTicket, *OptionalOutRGBA);
    }
    else
    {
        (void)DeviceInterchange->AwaitTimelineTick(Signal);
    }

    vkFreeCommandBuffers(DeviceHandle, DeviceInterchange->AccessCommandAllocator(), 1u, &Cmd);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D & 2D GPU PICKING & CAMERA RAY
//------------------------------------------------------------------------------------------------------------------------

ViewportPickHit UVWorkspaceRasterizer::Pick3DPixel(int32_t CursorX, int32_t CursorY)
{
    ViewportPickHit Hit = PickQuery.QuerySinglePixel(
        Target3D.PickImage.ImageHandle,
        Target3D.PickLayout,
        Target3D.WidthPx,
        Target3D.HeightPx,
        CursorX,
        CursorY);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Hit;
}

ViewportPickHit UVWorkspaceRasterizer::Pick2DPixel(int32_t CursorX, int32_t CursorY)
{
    ViewportPickHit Hit = PickQuery.QuerySinglePixel(
        Target2D.PickImage.ImageHandle,
        Target2D.PickLayout,
        Target2D.WidthPx,
        Target2D.HeightPx,
        CursorX,
        CursorY);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Hit;
}

RegionPickSummary UVWorkspaceRasterizer::Reduce3DRegionPick(
    uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
    uint32_t BrushCenterX, uint32_t BrushCenterY, uint32_t BrushRadiusPx)
{
    RegionPickSummary Sum = PickQuery.ExecuteRegionReduction(
        Target3D.PickImage.ImageHandle,
        Target3D.PickLayout,
        Target3D.WidthPx,
        Target3D.HeightPx,
        MinX, MinY, MaxX, MaxY,
        BrushCenterX, BrushCenterY, BrushRadiusPx,
        SelectionBitmaskBuffer.BufferHandle,
        UploadedFaceCount);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Sum;
}

RegionPickSummary UVWorkspaceRasterizer::Reduce2DRegionPick(
    uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
    uint32_t BrushCenterX, uint32_t BrushCenterY, uint32_t BrushRadiusPx)
{
    RegionPickSummary Sum = PickQuery.ExecuteRegionReduction(
        Target2D.PickImage.ImageHandle,
        Target2D.PickLayout,
        Target2D.WidthPx,
        Target2D.HeightPx,
        MinX, MinY, MaxX, MaxY,
        BrushCenterX, BrushCenterY, BrushRadiusPx,
        SelectionBitmaskBuffer.BufferHandle,
        UploadedFaceCount);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Sum;
}

Editor::ViewportRay3D UVWorkspaceRasterizer::ConstructCameraRay3D(
    const CameraOrbitRig3D& Camera,
    uint32_t                  WidthPx,
    uint32_t                  HeightPx,
    float                     CursorX,
    float                     CursorY) noexcept
{
    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Camera, Eye, Right, Up, Fwd);

    const float Aspect     = static_cast<float>(std::max(1u, WidthPx)) / static_cast<float>(std::max(1u, HeightPx));
    const float TanHalfFov = std::tan(Camera.VerticalFovDeg * Constants::DegToRadFloat * 0.5f);
    const float NdcX       = (CursorX / static_cast<float>(std::max(1u, WidthPx))) * 2.0f - 1.0f;
    const float NdcY       = 1.0f - (CursorY / static_cast<float>(std::max(1u, HeightPx))) * 2.0f;

    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(
        Fwd + Right * (NdcX * Aspect * TanHalfFov) + Up * (NdcY * TanHalfFov));
    return Editor::ViewportRay3D{ Eye, Dir };
}

} // namespace Frontier::GeometricRaster
