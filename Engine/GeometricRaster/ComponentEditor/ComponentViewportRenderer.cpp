/*============================================================================================================================================
                                                     COMPONENTVIEWPORTRENDERER.CPP
============================================================================================================================================*/
// 🧩 Implementation of Vulkan 1.3 OpenPbrDirect (Zero GI) + 7-channel .slang inspection & Layer-1 X-ray constraint renderer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/Constants/MathConstants.h"

#include <volk.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace Frontier::GeometricRaster::ComponentEditor
{

namespace
{

constexpr float DegToRad = static_cast<float>(Constants::Pi) / 180.0f;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHADER STRUCT LAYOUTS
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ComponentSceneLightGpu
{
    float PositionRange[4];
    float DirectionCosine[4];
    float ColorIntensity[4];
};

struct alignas(16) ComponentViewportUniformsGpu
{
    float    ViewProjection[16];
    float    CameraPositionTime[4];
    float    SunDirectionExposure[4];
    float    FocusPointRadius[4];
    float    HaloTintParams[4];
    uint32_t ActiveCounts[4];
    ComponentSceneLightGpu SceneLights[8];
};

static_assert(sizeof(ComponentViewportUniformsGpu) == 528u);

struct ComponentSurfacePushConstantsGpu
{
    float    WorldTransform[16];
    float    AlbedoRoughness[4];
    float    EmissiveMetalness[4];
    float    ChannelModifiers[4];
    uint32_t PickPacked[4];
};

static_assert(sizeof(ComponentSurfacePushConstantsGpu) == 128u);

//------------------------------------------------------------------------------------------------------------------------
//                                                     4x4 MATRIX MATH
//------------------------------------------------------------------------------------------------------------------------

struct Mat4
{
    float M[16] {}; // Row-major 4x4 matching slang -matrix-layout-row-major
};

[[nodiscard]] Mat4 Mat4Identity() noexcept
{
    Mat4 R {};
    R.M[0] = R.M[5] = R.M[10] = R.M[15] = 1.0f;
    return R;
}

[[nodiscard]] Mat4 Mat4Multiply(const Mat4& A, const Mat4& B) noexcept
{
    Mat4 R {};
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            float Sum = 0.0f;
            for (int K = 0; K < 4; ++K)
            {
                Sum += A.M[Row * 4 + K] * B.M[K * 4 + Col];
            }
            R.M[Row * 4 + Col] = Sum;
        }
    }
    return R;
}

[[nodiscard]] Mat4 Mat4FromTransform(const ComponentClass::ComponentTransform& T) noexcept
{
    const float RX = T.RotationDegrees.X * DegToRad;
    const float RY = T.RotationDegrees.Y * DegToRad;
    const float RZ = T.RotationDegrees.Z * DegToRad;
    const float CX = std::cos(RX), SX = std::sin(RX);
    const float CY = std::cos(RY), SY = std::sin(RY);
    const float CZ = std::cos(RZ), SZ = std::sin(RZ);

    // Euler YZX intrinsic matching ComponentViewer / Three.js
    const float R00 = CY * CZ;
    const float R01 = CZ * SX * SY - CX * SZ;
    const float R02 = CX * CZ * SY + SX * SZ;
    const float R10 = CY * SZ;
    const float R11 = CX * CZ + SX * SY * SZ;
    const float R12 = -CZ * SX + CX * SY * SZ;
    const float R20 = -SY;
    const float R21 = CY * SX;
    const float R22 = CX * CY;

    Mat4 Out {};
    Out.M[0]  = R00 * T.ScaleFactors.X; Out.M[1]  = R01 * T.ScaleFactors.Y; Out.M[2]  = R02 * T.ScaleFactors.Z; Out.M[3]  = T.LocationMeters.X;
    Out.M[4]  = R10 * T.ScaleFactors.X; Out.M[5]  = R11 * T.ScaleFactors.Y; Out.M[6]  = R12 * T.ScaleFactors.Z; Out.M[7]  = T.LocationMeters.Y;
    Out.M[8]  = R20 * T.ScaleFactors.X; Out.M[9]  = R21 * T.ScaleFactors.Y; Out.M[10] = R22 * T.ScaleFactors.Z; Out.M[11] = T.LocationMeters.Z;
    Out.M[15] = 1.0f;
    return Out;
}

[[nodiscard]] Mat4 Mat4LookAtRh(
    Mathematics::Axis3D Eye,
    Mathematics::Axis3D Target,
    Mathematics::Axis3D UpRef) noexcept
{
    auto Norm = [](Mathematics::Axis3D V) -> Mathematics::Axis3D
    {
        const float L = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
        return (L > 1.0e-6f) ? Mathematics::Axis3D{ V.X / L, V.Y / L, V.Z / L } : Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f };
    };
    auto Cross = [](Mathematics::Axis3D A, Mathematics::Axis3D B) -> Mathematics::Axis3D
    {
        return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
    };
    auto Dot = [](Mathematics::Axis3D A, Mathematics::Axis3D B) -> float
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    };

    const Mathematics::Axis3D F = Norm({ Target.X - Eye.X, Target.Y - Eye.Y, Target.Z - Eye.Z });
    const Mathematics::Axis3D S = Norm(Cross(F, UpRef));
    const Mathematics::Axis3D U = Cross(S, F);

    Mat4 R {};
    R.M[0]  = S.X;  R.M[1]  = S.Y;  R.M[2]  = S.Z;  R.M[3]  = -Dot(S, Eye);
    R.M[4]  = U.X;  R.M[5]  = U.Y;  R.M[6]  = U.Z;  R.M[7]  = -Dot(U, Eye);
    R.M[8]  = -F.X; R.M[9]  = -F.Y; R.M[10] = -F.Z; R.M[11] = Dot(F, Eye);
    R.M[15] = 1.0f;
    return R;
}

[[nodiscard]] Mat4 Mat4PerspectiveVulkan(float VerticalFovDeg, float Aspect, float NearZ, float FarZ) noexcept
{
    const float F = 1.0f / std::tan((VerticalFovDeg * DegToRad) * 0.5f);
    Mat4 R {};
    R.M[0]  = F / std::max(Aspect, 0.1f);
    R.M[5]  = -F; // Vulkan clip Y points down
    R.M[10] = FarZ / (NearZ - FarZ);
    R.M[11] = (NearZ * FarZ) / (NearZ - FarZ);
    R.M[14] = -1.0f;
    return R;
}

[[nodiscard]] Mathematics::Axis3D TransformPoint(const Mat4& M, const Mathematics::Axis3D& P) noexcept
{
    return {
        M.M[0] * P.X + M.M[1] * P.Y + M.M[2]  * P.Z + M.M[3],
        M.M[4] * P.X + M.M[5] * P.Y + M.M[6]  * P.Z + M.M[7],
        M.M[8] * P.X + M.M[9] * P.Y + M.M[10] * P.Z + M.M[11]
    };
}

[[nodiscard]] Mathematics::Axis3D TransformDirection(const Mat4& M, const Mathematics::Axis3D& D) noexcept
{
    const Mathematics::Axis3D V {
        M.M[0] * D.X + M.M[1] * D.Y + M.M[2]  * D.Z,
        M.M[4] * D.X + M.M[5] * D.Y + M.M[6]  * D.Z,
        M.M[8] * D.X + M.M[9] * D.Y + M.M[10] * D.Z
    };
    const float L = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
    return (L > 1.0e-6f) ? Mathematics::Axis3D{ V.X / L, V.Y / L, V.Z / L } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
}

[[nodiscard]] Mat4 Mat4FromAxisAngle(const Mathematics::Axis3D& Axis, float AngleRad) noexcept
{
    const float C = std::cos(AngleRad), S = std::sin(AngleRad), OneC = 1.0f - C;
    const float X = Axis.X, Y = Axis.Y, Z = Axis.Z;
    Mat4 R = Mat4Identity();
    R.M[0] = C + X * X * OneC;     R.M[1] = X * Y * OneC - Z * S; R.M[2]  = X * Z * OneC + Y * S;
    R.M[4] = Y * X * OneC + Z * S; R.M[5] = C + Y * Y * OneC;     R.M[6]  = Y * Z * OneC - X * S;
    R.M[8] = Z * X * OneC - Y * S; R.M[9] = Z * Y * OneC + X * S; R.M[10] = C + Z * Z * OneC;
    return R;
}

[[nodiscard]] Mat4 Mat4Translation(const Mathematics::Axis3D& T) noexcept
{
    Mat4 R = Mat4Identity();
    R.M[3] = T.X; R.M[7] = T.Y; R.M[11] = T.Z;
    return R;
}

[[nodiscard]] Mat4 Mat4Scale(const Mathematics::Scalar3D& S) noexcept
{
    Mat4 R {};
    R.M[0] = S.X; R.M[5] = S.Y; R.M[10] = S.Z; R.M[15] = 1.0f;
    return R;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU MESH & PIPELINE RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct GpuVertexSliceUpload
{
    DeviceBufferResource Buffer {};
    uint32_t             VertexCount = 0u;
};

struct CachedAssetGpuEntry
{
    uint64_t                    Revision = 0ull;
    CompiledGeometryVisualBatch Batch {};
    GpuVertexSliceUpload        FullBuf {};
    GpuVertexSliceUpload        DensityBuf {};
    GpuVertexSliceUpload        LodBuf {};
};

} // namespace

struct ComponentViewportRenderer::RendererImplementation
{
    VulkanInterchange* DeviceInterchange = nullptr;
    VkDevice           Device            = VK_NULL_HANDLE;
    bool               Ready             = false;

    PFN_vkCmdBeginRendering   DynBeginRender = nullptr;
    PFN_vkCmdEndRendering     DynEndRender   = nullptr;
    PFN_vkCmdPipelineBarrier2 Sync2Barrier   = nullptr;

    VkDescriptorSetLayout DescLayout = VK_NULL_HANDLE;
    VkPipelineLayout      PipeLayout = VK_NULL_HANDLE;
    VkDescriptorPool      DescriptorArena   = VK_NULL_HANDLE;
    VkDescriptorSet       DescSet    = VK_NULL_HANDLE;

    VkPipeline ChannelOpaquePipe      = VK_NULL_HANDLE;
    VkPipeline ChannelAlphaPipe       = VK_NULL_HANDLE;
    VkPipeline FieldHullRaster         = VK_NULL_HANDLE;
    VkPipeline LightVolumePipe        = VK_NULL_HANDLE;
    VkPipeline DotFloorPipe           = VK_NULL_HANDLE;
    VkPipeline ConstraintSolidPipe    = VK_NULL_HANDLE;
    VkPipeline ConstraintAdditivePipe = VK_NULL_HANDLE;

    DeviceBufferResource UniformBuffer {};

    GpuVertexSliceUpload DotFloorBuf      {};
    GpuVertexSliceUpload UnitBoxBuf       {};
    GpuVertexSliceUpload UnitSphereBuf    {};
    GpuVertexSliceUpload OriginRingBuf    {};
    GpuVertexSliceUpload ArrowUnitBuf     {};
    GpuVertexSliceUpload CameraFrustumBuf {};

    std::unordered_map<std::string, CachedAssetGpuEntry> AssetCache {};

    uint32_t              TargetW = 0u;
    uint32_t              TargetH = 0u;
    DeviceImageResource   ColorTarget {};
    DeviceImageResource   PickTarget  {};
    DeviceImageResource   DepthTarget {};
    DeviceBufferResource  ColorReadback {};
    DeviceBufferResource  PickReadback  {};
    std::vector<uint32_t> CpuPickRgba32ui {};

    std::vector<DeviceBufferResource> TransientOutputBuffers {};

    void UploadVertexBuffer(std::span<const ComponentShaderVertex> Verts, GpuVertexSliceUpload& Out)
    {
        if (Out.Buffer.BufferHandle != VK_NULL_HANDLE)
        {
            DeviceInterchange->ReleaseBuffer(Out.Buffer);
        }
        Out.VertexCount = static_cast<uint32_t>(Verts.size());
        if (Verts.empty())
        {
            return;
        }
        const uint64_t Bytes = static_cast<uint64_t>(Verts.size()) * sizeof(ComponentShaderVertex);
        Out.Buffer = DeviceInterchange->AllocateBuffer(
            Bytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (Out.Buffer.HostAddress != nullptr)
        {
            std::memcpy(Out.Buffer.HostAddress, Verts.data(), Bytes);
        }
    }

    [[nodiscard]] VkBuffer UploadTransientVertices(std::span<const ComponentShaderVertex> Verts)
    {
        if (Verts.empty())
        {
            return VK_NULL_HANDLE;
        }
        const uint64_t Bytes = static_cast<uint64_t>(Verts.size()) * sizeof(ComponentShaderVertex);
        DeviceBufferResource Buf = DeviceInterchange->AllocateBuffer(
            Bytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (Buf.BufferHandle == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        if (Buf.HostAddress != nullptr)
        {
            std::memcpy(Buf.HostAddress, Verts.data(), Bytes);
        }
        TransientOutputBuffers.push_back(Buf);
        return Buf.BufferHandle;
    }

    void ReleaseTransientBuffers() noexcept
    {
        for (auto& Buf : TransientOutputBuffers)
        {
            DeviceInterchange->ReleaseBuffer(Buf);
        }
        TransientOutputBuffers.clear();
    }

    void ReleaseAssetCache() noexcept
    {
        for (auto& [Key, Rec] : AssetCache)
        {
            if (Rec.FullBuf.Buffer.BufferHandle != VK_NULL_HANDLE)    DeviceInterchange->ReleaseBuffer(Rec.FullBuf.Buffer);
            if (Rec.DensityBuf.Buffer.BufferHandle != VK_NULL_HANDLE) DeviceInterchange->ReleaseBuffer(Rec.DensityBuf.Buffer);
            if (Rec.LodBuf.Buffer.BufferHandle != VK_NULL_HANDLE)     DeviceInterchange->ReleaseBuffer(Rec.LodBuf.Buffer);
        }
        AssetCache.clear();
    }

    void ReleaseRenderTargets() noexcept
    {
        if (DeviceInterchange == nullptr)
        {
            return;
        }
        DeviceInterchange->ReleaseImage2D(ColorTarget);
        DeviceInterchange->ReleaseImage2D(PickTarget);
        DeviceInterchange->ReleaseImage2D(DepthTarget);
        DeviceInterchange->ReleaseBuffer(ColorReadback);
        DeviceInterchange->ReleaseBuffer(PickReadback);
        TargetW = 0u;
        TargetH = 0u;
    }

    [[nodiscard]] bool EnsureRenderTargets(uint32_t W, uint32_t H)
    {
        if (TargetW == W && TargetH == H && ColorTarget.ImageHandle != VK_NULL_HANDLE)
        {
            return true;
        }
        ReleaseRenderTargets();

        ColorTarget = DeviceInterchange->AllocateImage2D(
            W, H, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        PickTarget = DeviceInterchange->AllocateImage2D(
            W, H, VK_FORMAT_R32G32B32A32_UINT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        DepthTarget = DeviceInterchange->AllocateImage2D(
            W, H, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

        ColorReadback = DeviceInterchange->AllocateBuffer(
            static_cast<uint64_t>(W) * H * 4ull,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        PickReadback = DeviceInterchange->AllocateBuffer(
            static_cast<uint64_t>(W) * H * 16ull,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (ColorTarget.ViewHandle == VK_NULL_HANDLE ||
            PickTarget.ViewHandle == VK_NULL_HANDLE ||
            DepthTarget.ViewHandle == VK_NULL_HANDLE)
        {
            ReleaseRenderTargets();
            return false;
        }
        TargetW = W;
        TargetH = H;
        return true;
    }

    enum class BlendRecipe : uint8_t { Opaque, AlphaBlend, Additive };

    [[nodiscard]] VkPipeline ConstructGraphicsProgram(
        const std::filesystem::path& VertSpv,
        const std::filesystem::path& FragSpv,
        bool                         DepthWrite,
        bool                         BackfaceCull,
        BlendRecipe                  Blend)
    {
        const VkShaderModule VertMod = DeviceInterchange->LoadShaderBinary(VertSpv);
        const VkShaderModule FragMod = DeviceInterchange->LoadShaderBinary(FragSpv);
        if (VertMod == VK_NULL_HANDLE || FragMod == VK_NULL_HANDLE)
        {
            if (VertMod != VK_NULL_HANDLE) vkDestroyShaderModule(Device, VertMod, nullptr);
            if (FragMod != VK_NULL_HANDLE) vkDestroyShaderModule(Device, FragMod, nullptr);
            return VK_NULL_HANDLE;
        }

        VkPipelineShaderStageCreateInfo ShaderEntries[2] {};
        ShaderEntries[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ShaderEntries[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        ShaderEntries[0].module = VertMod;
        ShaderEntries[0].pName  = "VertexMain";
        ShaderEntries[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ShaderEntries[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        ShaderEntries[1].module = FragMod;
        ShaderEntries[1].pName  = "FragmentMain";

        VkVertexInputBindingDescription Binding {};
        Binding.binding   = 0u;
        Binding.stride    = sizeof(ComponentShaderVertex);
        Binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 4> Attrs {};
        Attrs[0] = { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ComponentShaderVertex, PositionMeters) };
        Attrs[1] = { 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ComponentShaderVertex, NormalVector) };
        Attrs[2] = { 2u, 0u, VK_FORMAT_R32G32_SFLOAT,    offsetof(ComponentShaderVertex, ParameterUv) };
        Attrs[3] = { 3u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ComponentShaderVertex, AuxColor) };

        VkPipelineVertexInputStateCreateInfo VertInput {};
        VertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VertInput.vertexBindingDescriptionCount   = 1u;
        VertInput.pVertexBindingDescriptions      = &Binding;
        VertInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(Attrs.size());
        VertInput.pVertexAttributeDescriptions    = Attrs.data();

        VkPipelineInputAssemblyStateCreateInfo InputAsm {};
        InputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo ViewportOptions {};
        ViewportOptions.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewportOptions.viewportCount = 1u;
        ViewportOptions.scissorCount  = 1u;

        VkPipelineRasterizationStateCreateInfo Raster {};
        Raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        Raster.polygonMode = VK_POLYGON_MODE_FILL;
        Raster.cullMode    = BackfaceCull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        Raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo Msaa {};
        Msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        Msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo Depth {};
        Depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        Depth.depthTestEnable  = VK_TRUE;
        Depth.depthWriteEnable = DepthWrite ? VK_TRUE : VK_FALSE;
        Depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        std::array<VkPipelineColorBlendAttachmentState, 2> Blends {};
        Blends[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (Blend == BlendRecipe::AlphaBlend)
        {
            Blends[0].blendEnable         = VK_TRUE;
            Blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            Blends[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            Blends[0].colorBlendOp        = VK_BLEND_OP_ADD;
            Blends[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            Blends[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            Blends[0].alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        else if (Blend == BlendRecipe::Additive)
        {
            Blends[0].blendEnable         = VK_TRUE;
            Blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            Blends[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            Blends[0].colorBlendOp        = VK_BLEND_OP_ADD;
            Blends[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            Blends[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            Blends[0].alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        Blends[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        Blends[1].blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo BlendSpec {};
        BlendSpec.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        BlendSpec.attachmentCount = static_cast<uint32_t>(Blends.size());
        BlendSpec.pAttachments    = Blends.data();

        const std::array<VkDynamicState, 2> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
        VkPipelineDynamicStateCreateInfo DynamicSpec {};
        DynamicSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynamicSpec.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
        DynamicSpec.pDynamicStates    = DynStates.data();

        const std::array<VkFormat, 2> ColorFormats {{ VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_UINT }};
        VkPipelineRenderingCreateInfo RenderSpec {};
        RenderSpec.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        RenderSpec.colorAttachmentCount    = static_cast<uint32_t>(ColorFormats.size());
        RenderSpec.pColorAttachmentFormats = ColorFormats.data();
        RenderSpec.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo ProgramSpec {};
        ProgramSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ProgramSpec.pNext               = &RenderSpec;
        ProgramSpec.stageCount          = 2u;
        ProgramSpec.pStages             = ShaderEntries;
        ProgramSpec.pVertexInputState   = &VertInput;
        ProgramSpec.pInputAssemblyState = &InputAsm;
        ProgramSpec.pViewportState      = &ViewportOptions;
        ProgramSpec.pRasterizationState = &Raster;
        ProgramSpec.pMultisampleState   = &Msaa;
        ProgramSpec.pDepthStencilState  = &Depth;
        ProgramSpec.pColorBlendState    = &BlendSpec;
        ProgramSpec.pDynamicState       = &DynamicSpec;
        ProgramSpec.layout              = PipeLayout;

        VkPipeline Result = VK_NULL_HANDLE;
        (void)vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &ProgramSpec, nullptr, &Result);

        vkDestroyShaderModule(Device, VertMod, nullptr);
        vkDestroyShaderModule(Device, FragMod, nullptr);
        return Result;
    }

    void TransitionImage(
        VkCommandBuffer       Cmd,
        VkImage               Image,
        VkImageAspectFlags    Aspect,
        VkImageLayout         OldLayout,
        VkImageLayout         NewLayout,
        VkPipelineStageFlags2 SrcBarrierMask,
        VkAccessFlags2        SrcAccess,
        VkPipelineStageFlags2 DstBarrierMask,
        VkAccessFlags2        DstAccess) const
    {
        VkImageMemoryBarrier2 Barrier {};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        Barrier.srcStageMask                    = SrcBarrierMask;
        Barrier.srcAccessMask                   = SrcAccess;
        Barrier.dstStageMask                    = DstBarrierMask;
        Barrier.dstAccessMask                   = DstAccess;
        Barrier.oldLayout                       = OldLayout;
        Barrier.newLayout                       = NewLayout;
        Barrier.image                           = Image;
        Barrier.subresourceRange.aspectMask     = Aspect;
        Barrier.subresourceRange.baseMipLevel   = 0u;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.baseArrayLayer = 0u;
        Barrier.subresourceRange.layerCount     = 1u;

        VkDependencyInfo Dep {};
        Dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        Dep.imageMemoryBarrierCount = 1u;
        Dep.pImageMemoryBarriers    = &Barrier;
        Sync2Barrier(Cmd, &Dep);
    }

    const CachedAssetGpuEntry* AcquireAssetEntry(const ComponentClass::CompiledGeometryRepositoryEntry& Entry)
    {
        auto It = AssetCache.find(Entry.AssetIdentifier);
        if (It != AssetCache.end() && It->second.Revision == Entry.Asset.Network.NetworkRevision)
        {
            return &It->second;
        }
        CachedAssetGpuEntry& Rec = AssetCache[Entry.AssetIdentifier];
        Rec.Revision = Entry.Asset.Network.NetworkRevision;
        Rec.Batch    = ComponentGizmoGeometry::BuildCompiledAssetBatch(Entry);
        UploadVertexBuffer(Rec.Batch.FullDetailVertices, Rec.FullBuf);
        UploadVertexBuffer(Rec.Batch.DensityVertices, Rec.DensityBuf);
        UploadVertexBuffer(Rec.Batch.LodVertices, Rec.LodBuf);
        return &Rec;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 LIFECYCLE & PUBLIC API
//------------------------------------------------------------------------------------------------------------------------

ComponentViewportRenderer::ComponentViewportRenderer()
    : Impl(std::make_unique<RendererImplementation>())
{
}

ComponentViewportRenderer::~ComponentViewportRenderer()
{
    TeardownVulkan();
}

bool ComponentViewportRenderer::InitializeVulkan(
    VulkanInterchange&           TargetDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownVulkan();
    if (!TargetDevice.Ready())
    {
        return false;
    }

    Impl->DeviceInterchange = &TargetDevice;
    Impl->Device            = TargetDevice.AccessDevice();
    Impl->DynBeginRender    = TargetDevice.QueryUsesKhrDynamicRendering() ? vkCmdBeginRenderingKHR : vkCmdBeginRendering;
    Impl->DynEndRender      = TargetDevice.QueryUsesKhrDynamicRendering() ? vkCmdEndRenderingKHR : vkCmdEndRendering;
    Impl->Sync2Barrier      = TargetDevice.QueryUsesKhrSynchronization2() ? vkCmdPipelineBarrier2KHR : vkCmdPipelineBarrier2;
    if (Impl->DynBeginRender == nullptr || Impl->DynEndRender == nullptr || Impl->Sync2Barrier == nullptr)
    {
        TeardownVulkan();
        return false;
    }

    VkDescriptorSetLayoutBinding UniformSlotIdx {};
    UniformSlotIdx.binding         = 0u;
    UniformSlotIdx.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UniformSlotIdx.descriptorCount = 1u;
    UniformSlotIdx.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutSpec {};
    LayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutSpec.bindingCount = 1u;
    LayoutSpec.pBindings    = &UniformSlotIdx;
    if (vkCreateDescriptorSetLayout(Impl->Device, &LayoutSpec, nullptr, &Impl->DescLayout) != VK_SUCCESS)
    {
        TeardownVulkan();
        return false;
    }

    VkPushConstantRange PushRange {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(ComponentSurfacePushConstantsGpu);

    VkPipelineLayoutCreateInfo ProgramLayoutSpec {};
    ProgramLayoutSpec.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ProgramLayoutSpec.setLayoutCount         = 1u;
    ProgramLayoutSpec.pSetLayouts            = &Impl->DescLayout;
    ProgramLayoutSpec.pushConstantRangeCount = 1u;
    ProgramLayoutSpec.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Impl->Device, &ProgramLayoutSpec, nullptr, &Impl->PipeLayout) != VK_SUCCESS)
    {
        TeardownVulkan();
        return false;
    }

    VkDescriptorPoolSize DescriptorCountSpec { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u };
    VkDescriptorPoolCreateInfo ArenaSpec {};
    ArenaSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ArenaSpec.maxSets       = 1u;
    ArenaSpec.poolSizeCount = 1u;
    ArenaSpec.pPoolSizes    = &DescriptorCountSpec;
    if (vkCreateDescriptorPool(Impl->Device, &ArenaSpec, nullptr, &Impl->DescriptorArena) != VK_SUCCESS)
    {
        TeardownVulkan();
        return false;
    }

    VkDescriptorSetAllocateInfo AllocSpec {};
    AllocSpec.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocSpec.descriptorPool     = Impl->DescriptorArena;
    AllocSpec.descriptorSetCount = 1u;
    AllocSpec.pSetLayouts        = &Impl->DescLayout;
    if (vkAllocateDescriptorSets(Impl->Device, &AllocSpec, &Impl->DescSet) != VK_SUCCESS)
    {
        TeardownVulkan();
        return false;
    }

    Impl->UniformBuffer = TargetDevice.AllocateBuffer(
        sizeof(ComponentViewportUniformsGpu),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (Impl->UniformBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownVulkan();
        return false;
    }

    VkDescriptorBufferInfo BufferSpec { Impl->UniformBuffer.BufferHandle, 0ull, sizeof(ComponentViewportUniformsGpu) };
    VkWriteDescriptorSet Write {};
    Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet          = Impl->DescSet;
    Write.dstBinding      = 0u;
    Write.descriptorCount = 1u;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Write.pBufferInfo     = &BufferSpec;
    vkUpdateDescriptorSets(Impl->Device, 1u, &Write, 0u, nullptr);

    using BR = RendererImplementation::BlendRecipe;
    const auto Spv = [&](const char* File) { return CompiledShaderDirectory / File; };
    Impl->ChannelOpaquePipe      = Impl->ConstructGraphicsProgram(Spv("ComponentChannelInspection.vert.spv"), Spv("ComponentChannelInspection.frag.spv"), true,  true,  BR::Opaque);
    Impl->ChannelAlphaPipe       = Impl->ConstructGraphicsProgram(Spv("ComponentChannelInspection.vert.spv"), Spv("ComponentChannelInspection.frag.spv"), false, false, BR::AlphaBlend);
    Impl->FieldHullRaster         = Impl->ConstructGraphicsProgram(Spv("ComponentFieldHull.vert.spv"),        Spv("ComponentFieldHull.frag.spv"),        false, false, BR::AlphaBlend);
    Impl->LightVolumePipe        = Impl->ConstructGraphicsProgram(Spv("ComponentLightVolume.vert.spv"),       Spv("ComponentLightVolume.frag.spv"),       false, false, BR::Additive);
    Impl->DotFloorPipe           = Impl->ConstructGraphicsProgram(Spv("ComponentDotFloor.vert.spv"),          Spv("ComponentDotFloor.frag.spv"),          false, false, BR::AlphaBlend);
    Impl->ConstraintSolidPipe    = Impl->ConstructGraphicsProgram(Spv("ComponentConstraintVisual.vert.spv"),  Spv("ComponentConstraintVisual.frag.spv"),  true,  true,  BR::Opaque);
    Impl->ConstraintAdditivePipe = Impl->ConstructGraphicsProgram(Spv("ComponentConstraintVisual.vert.spv"),  Spv("ComponentConstraintVisual.frag.spv"),  false, false, BR::Additive);

    if (Impl->ChannelOpaquePipe == VK_NULL_HANDLE ||
        Impl->FieldHullRaster == VK_NULL_HANDLE ||
        Impl->DotFloorPipe == VK_NULL_HANDLE ||
        Impl->ConstraintSolidPipe == VK_NULL_HANDLE)
    {
        TeardownVulkan();
        return false;
    }

    Impl->UploadVertexBuffer(ComponentGizmoGeometry::BuildDotFloorQuad(120.0f), Impl->DotFloorBuf);
    Impl->UploadVertexBuffer(ComponentGizmoGeometry::ConstructUnitBoxHull(), Impl->UnitBoxBuf);
    Impl->UploadVertexBuffer(ComponentGizmoGeometry::ConstructUnitSphereHull(), Impl->UnitSphereBuf);
    Impl->UploadVertexBuffer(ComponentGizmoGeometry::BuildOriginRing(0.14f), Impl->OriginRingBuf);
    Impl->UploadVertexBuffer(ComponentGizmoGeometry::BuildSolidArrow(1.0f), Impl->ArrowUnitBuf);
    Impl->UploadVertexBuffer(ComponentGizmoGeometry::BuildCameraFrustumBeam(1.8f, 52.0f, 16.0f / 9.0f), Impl->CameraFrustumBuf);

    Impl->Ready = true;
    return true;
}

void ComponentViewportRenderer::TeardownVulkan() noexcept
{
    if (Impl == nullptr)
    {
        return;
    }
    if (Impl->Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Impl->Device);
        Impl->ReleaseTransientBuffers();
        Impl->ReleaseAssetCache();
        Impl->ReleaseRenderTargets();

        auto DestroyBuf = [&](GpuVertexSliceUpload& U)
        {
            if (U.Buffer.BufferHandle != VK_NULL_HANDLE) Impl->DeviceInterchange->ReleaseBuffer(U.Buffer);
            U.VertexCount = 0u;
        };
        DestroyBuf(Impl->DotFloorBuf);
        DestroyBuf(Impl->UnitBoxBuf);
        DestroyBuf(Impl->UnitSphereBuf);
        DestroyBuf(Impl->OriginRingBuf);
        DestroyBuf(Impl->ArrowUnitBuf);
        DestroyBuf(Impl->CameraFrustumBuf);
        Impl->DeviceInterchange->ReleaseBuffer(Impl->UniformBuffer);

        auto DestroyPipe = [&](VkPipeline& P)
        {
            if (P != VK_NULL_HANDLE) { vkDestroyPipeline(Impl->Device, P, nullptr); P = VK_NULL_HANDLE; }
        };
        DestroyPipe(Impl->ChannelOpaquePipe);
        DestroyPipe(Impl->ChannelAlphaPipe);
        DestroyPipe(Impl->FieldHullRaster);
        DestroyPipe(Impl->LightVolumePipe);
        DestroyPipe(Impl->DotFloorPipe);
        DestroyPipe(Impl->ConstraintSolidPipe);
        DestroyPipe(Impl->ConstraintAdditivePipe);

        if (Impl->DescriptorArena != VK_NULL_HANDLE)   { vkDestroyDescriptorPool(Impl->Device, Impl->DescriptorArena, nullptr);       Impl->DescriptorArena = VK_NULL_HANDLE; }
        if (Impl->PipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Impl->Device, Impl->PipeLayout, nullptr);     Impl->PipeLayout = VK_NULL_HANDLE; }
        if (Impl->DescLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Impl->Device, Impl->DescLayout, nullptr); Impl->DescLayout = VK_NULL_HANDLE; }
    }
    Impl->Device            = VK_NULL_HANDLE;
    Impl->DeviceInterchange = nullptr;
    Impl->Ready             = false;
}

bool ComponentViewportRenderer::QueryVulkanReady() const noexcept
{
    return Impl != nullptr && Impl->Ready;
}

void ComponentViewportRenderer::ClearGeometryCache() noexcept
{
    if (Impl != nullptr && Impl->Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Impl->Device);
        Impl->ReleaseAssetCache();
    }
}

ComponentViewportCamera ComponentViewportRenderer::ComputeFramedCameraForClass(
    const ComponentClass::ComponentClassDefinition&               ClassDef,
    std::span<const ComponentClass::CompiledGeometryRepositoryEntry> Repository,
    float                                                         YawRadians,
    float                                                         PitchRadians) const
{
    std::unordered_map<std::string_view, const ComponentClass::CompiledGeometryRepositoryEntry*> RepositoryLookup {};
    for (const auto& Entry : Repository)
    {
        RepositoryLookup[Entry.AssetIdentifier] = &Entry;
    }

    const uint32_t RawCount = ClassDef.Storage.QueryRawSlotCount();
    std::vector<Mat4> WorldMats(RawCount + 1u, Mat4Identity());
    std::vector<const ComponentClass::ComponentInstanceHeader*> Ordered {};
    Ordered.reserve(RawCount);

    ClassDef.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        Ordered.push_back(&H);
        const Mat4 Local = Mat4FromTransform(H.LocalTransform);
        if (H.EnclosingIndex == ComponentClass::InvalidNestingIndex)
        {
            WorldMats[H.Token.SlotIndex] = Local;
        }
        else if (const auto* Enc = ClassDef.Storage.InspectHeaderByIndex(H.EnclosingIndex))
        {
            WorldMats[H.Token.SlotIndex] = Mat4Multiply(WorldMats[Enc->Token.SlotIndex], Local);
        }
    });

    Mathematics::Axis3D MinB {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxB { -1.0e9f, -1.0e9f, -1.0e9f };
    bool BoundsPresent = false;

    for (const auto* HPtr : Ordered)
    {
        const auto& H = *HPtr;
        if (H.Category != ComponentClass::ComponentCategory::GeometryInstance || !H.VisibilityEnabled)
        {
            continue;
        }
        const auto* Geom = ClassDef.Storage.InspectGeometry(H.Token);
        if (Geom == nullptr) continue;
        const auto CatIt = RepositoryLookup.find(Geom->AssetIdentifier);
        if (CatIt == RepositoryLookup.end()) continue;

        for (const auto& V : CatIt->second->Asset.Network.Vertices)
        {
            const Mathematics::Axis3D W = TransformPoint(WorldMats[H.Token.SlotIndex], V.Axes);
            MinB.X = std::min(MinB.X, W.X); MinB.Y = std::min(MinB.Y, W.Y); MinB.Z = std::min(MinB.Z, W.Z);
            MaxB.X = std::max(MaxB.X, W.X); MaxB.Y = std::max(MaxB.Y, W.Y); MaxB.Z = std::max(MaxB.Z, W.Z);
            BoundsPresent = true;
        }
    }

    ComponentViewportCamera Cam {};
    Cam.YawRadians   = YawRadians;
    Cam.PitchRadians = PitchRadians;
    if (BoundsPresent)
    {
        Cam.FocusPointMeters = {
            0.5f * (MinB.X + MaxB.X),
            0.5f * (MinB.Y + MaxB.Y),
            0.5f * (MinB.Z + MaxB.Z)
        };
        const float DX = MaxB.X - MinB.X;
        const float DY = MaxB.Y - MinB.Y;
        const float DZ = MaxB.Z - MinB.Z;
        const float Radius = std::max(0.5f * std::sqrt(DX * DX + DY * DY + DZ * DZ), 0.5f);
        Cam.DistanceMeters = (Radius / std::sin((Cam.VerticalFovDeg * DegToRad) * 0.5f)) * 0.88f;
    }
    return Cam;
}

ComponentViewportOutput ComponentViewportRenderer::RenderClassViewport(
    const ComponentClass::ComponentClassDefinition&               ClassDef,
    std::span<const ComponentClass::CompiledGeometryRepositoryEntry> Repository,
    const ComponentViewportCamera&                                Camera,
    const ComponentRenderOptions&                                 Options,
    uint32_t                                                      WidthPx,
    uint32_t                                                      HeightPx,
    bool                                                          PerformHostReadback)
{
    ComponentViewportOutput Result {};
    if (!QueryVulkanReady() || WidthPx == 0u || HeightPx == 0u)
    {
        return Result;
    }
    const auto StartClock = std::chrono::steady_clock::now();

    if (!Impl->EnsureRenderTargets(WidthPx, HeightPx))
    {
        return Result;
    }

    std::unordered_map<std::string_view, const CachedAssetGpuEntry*> GpuCatalog {};
    for (const auto& Entry : Repository)
    {
        GpuCatalog[Entry.AssetIdentifier] = Impl->AcquireAssetEntry(Entry);
    }

    const uint32_t RawCount = ClassDef.Storage.QueryRawSlotCount();
    std::vector<Mat4> WorldMats(RawCount + 1u, Mat4Identity());
    std::vector<Mat4> AnchorMats(RawCount + 1u, Mat4Identity());
    std::vector<const ComponentClass::ComponentInstanceHeader*> SpatialOrdered {};
    SpatialOrdered.reserve(RawCount);

    ClassDef.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        SpatialOrdered.push_back(&H);
        if (const auto OvIt = Options.SimulationWorldOverrides.find(H.Token.SlotIndex);
            OvIt != Options.SimulationWorldOverrides.end())
        {
            std::memcpy(WorldMats[H.Token.SlotIndex].M, OvIt->second.data(), sizeof(Mat4));
            AnchorMats[H.Token.SlotIndex] = WorldMats[H.Token.SlotIndex];
            return;
        }

        const Mat4 Local = Mat4FromTransform(H.LocalTransform);
        Mat4 PrimaryWorld = Local;
        if (H.EnclosingIndex != ComponentClass::InvalidNestingIndex)
        {
            if (const auto* Enc = ClassDef.Storage.InspectHeaderByIndex(H.EnclosingIndex))
            {
                PrimaryWorld = Mat4Multiply(WorldMats[Enc->Token.SlotIndex], Local);
            }
        }
        AnchorMats[H.Token.SlotIndex] = PrimaryWorld;

        if (H.Category == ComponentClass::ComponentCategory::ConstraintInstance)
        {
            if (const auto* Con = ClassDef.Storage.InspectConstraint(H.Token))
            {
                const float Coord = Options.LiveConstraintCoordinates.contains(H.Token.SlotIndex)
                    ? Options.LiveConstraintCoordinates.at(H.Token.SlotIndex)
                    : 0.0f;
                Mathematics::Axis3D Ax { 0.0f, 1.0f, 0.0f };
                if (Con->Axis == ComponentClass::ConstraintAxis::AxisX) Ax = { 1.0f, 0.0f, 0.0f };
                if (Con->Axis == ComponentClass::ConstraintAxis::AxisZ) Ax = { 0.0f, 0.0f, 1.0f };

                Mat4 PoseDelta = Mat4Identity();
                if (Con->Form == ComponentClass::PhysicsConstraintCategory::Hinge ||
                    Con->Form == ComponentClass::PhysicsConstraintCategory::Spin)
                {
                    PoseDelta = Mat4FromAxisAngle(Ax, Coord);
                }
                else if (Con->Form == ComponentClass::PhysicsConstraintCategory::Slider ||
                         Con->Form == ComponentClass::PhysicsConstraintCategory::Spring)
                {
                    PoseDelta = Mat4Translation({ Ax.X * Coord, Ax.Y * Coord, Ax.Z * Coord });
                }
                PrimaryWorld = Mat4Multiply(PrimaryWorld, PoseDelta);
            }
        }
        WorldMats[H.Token.SlotIndex] = PrimaryWorld;
    });

    Mathematics::Axis3D Eye {};
    Mathematics::Axis3D LookTarget = Camera.FocusPointMeters;
    float ActiveFov = Camera.VerticalFovDeg;

    if (Camera.PilotActive && Camera.PilotCameraToken.Valid())
    {
        if (const auto* CamComp = ClassDef.Storage.InspectCamera(Camera.PilotCameraToken))
        {
            const Mat4& CamM = WorldMats[Camera.PilotCameraToken.SlotIndex];
            Eye = TransformPoint(CamM, { 0.0f, 0.0f, 0.0f });
            const auto Fwd = TransformDirection(CamM, { 1.0f, 0.0f, 0.0f });
            LookTarget = { Eye.X + Fwd.X * 5.0f, Eye.Y + Fwd.Y * 5.0f, Eye.Z + Fwd.Z * 5.0f };
            ActiveFov  = CamComp->VerticalFovDegrees;
        }
    }
    else
    {
        const float Yaw = Camera.YawRadians + (Options.TurntableEnabled ? Options.ElapsedSeconds * 0.35f : 0.0f);
        const float CP  = std::cos(Camera.PitchRadians);
        const float SP  = std::sin(Camera.PitchRadians);
        Eye = {
            LookTarget.X + Camera.DistanceMeters * CP * std::cos(Yaw),
            LookTarget.Y + Camera.DistanceMeters * SP,
            LookTarget.Z + Camera.DistanceMeters * CP * std::sin(Yaw)
        };
    }

    const float Aspect = static_cast<float>(WidthPx) / static_cast<float>(HeightPx);
    const Mat4 View = Mat4LookAtRh(Eye, LookTarget, { 0.0f, 1.0f, 0.0f });
    const Mat4 Proj = Mat4PerspectiveVulkan(ActiveFov, Aspect, 0.05f, 400.0f);
    const Mat4 ViewProj = Mat4Multiply(Proj, View);

    ComponentViewportUniformsGpu Ubo {};
    std::memcpy(Ubo.ViewProjection, ViewProj.M, sizeof(Ubo.ViewProjection));
    Ubo.CameraPositionTime[0]   = Eye.X;
    Ubo.CameraPositionTime[1]   = Eye.Y;
    Ubo.CameraPositionTime[2]   = Eye.Z;
    Ubo.CameraPositionTime[3]   = Options.ElapsedSeconds;
    Ubo.SunDirectionExposure[0] = 0.42f;
    Ubo.SunDirectionExposure[1] = 0.78f;
    Ubo.SunDirectionExposure[2] = 0.46f;
    Ubo.SunDirectionExposure[3] = 1.05f;
    Ubo.FocusPointRadius[0]     = LookTarget.X;
    Ubo.FocusPointRadius[1]     = LookTarget.Y;
    Ubo.FocusPointRadius[2]     = LookTarget.Z;
    Ubo.FocusPointRadius[3]     = std::max(Camera.DistanceMeters * 2.4f, 12.0f);
    Ubo.HaloTintParams[0]       = 1.0f;
    Ubo.HaloTintParams[1]       = 0.698f;
    Ubo.HaloTintParams[2]       = 0.220f;
    Ubo.HaloTintParams[3]       = static_cast<float>(Options.Channel);

    uint32_t LightCount = 0u;
    for (const auto* HPtr : SpatialOrdered)
    {
        if (LightCount >= 8u) break;
        const auto& H = *HPtr;
        if (H.Category != ComponentClass::ComponentCategory::LightInstance || !H.VisibilityEnabled)
        {
            continue;
        }
        const auto* Lgt = ClassDef.Storage.InspectLight(H.Token);
        if (Lgt == nullptr) continue;
        const auto WPos = TransformPoint(WorldMats[H.Token.SlotIndex], { 0.0f, 0.0f, 0.0f });
        const auto WDir = TransformDirection(WorldMats[H.Token.SlotIndex], { 1.0f, 0.0f, 0.0f });
        auto& GpuL = Ubo.SceneLights[LightCount++];
        GpuL.PositionRange[0]   = WPos.X;
        GpuL.PositionRange[1]   = WPos.Y;
        GpuL.PositionRange[2]   = WPos.Z;
        GpuL.PositionRange[3]   = Lgt->AttenuationRangeMeters;
        GpuL.DirectionCosine[0] = WDir.X;
        GpuL.DirectionCosine[1] = WDir.Y;
        GpuL.DirectionCosine[2] = WDir.Z;
        GpuL.DirectionCosine[3] = (Lgt->Form == ComponentClass::LightSourceCategory::SpotBeam)
            ? std::cos(Lgt->ConeAngleDegrees * DegToRad)
            : -2.0f;
        GpuL.ColorIntensity[0]  = Lgt->LinearColourRGB.X;
        GpuL.ColorIntensity[1]  = Lgt->LinearColourRGB.Y;
        GpuL.ColorIntensity[2]  = Lgt->LinearColourRGB.Z;
        GpuL.ColorIntensity[3]  = Lgt->IntensityCandela * 0.006f;
    }
    Ubo.ActiveCounts[0] = LightCount;
    Ubo.ActiveCounts[1] = Options.SelectedToken.SlotIndex;
    Ubo.ActiveCounts[2] = static_cast<uint32_t>(Options.Channel);
    if (Impl->UniformBuffer.HostAddress != nullptr)
    {
        std::memcpy(Impl->UniformBuffer.HostAddress, &Ubo, sizeof(Ubo));
    }

    const VkCommandBuffer Cmd = Impl->DeviceInterchange->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return Result;
    }

    Impl->TransitionImage(
        Cmd, Impl->ColorTarget.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0u,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Impl->TransitionImage(
        Cmd, Impl->PickTarget.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0u,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    Impl->TransitionImage(
        Cmd, Impl->DepthTarget.ImageHandle, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0u,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    std::array<VkRenderingAttachmentInfo, 2> ColorAtts {};
    ColorAtts[0].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ColorAtts[0].imageView   = Impl->ColorTarget.ViewHandle;
    ColorAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAtts[0].loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAtts[0].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAtts[0].clearValue.color = Options.TransparentBackdrop
        ? VkClearColorValue{{ 0.0f, 0.0f, 0.0f, 0.0f }}
        : VkClearColorValue{{ 0.078f, 0.082f, 0.098f, 1.0f }};

    ColorAtts[1].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ColorAtts[1].imageView   = Impl->PickTarget.ViewHandle;
    ColorAtts[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAtts[1].loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAtts[1].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo DepthAtt {};
    DepthAtt.sType                         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    DepthAtt.imageView                     = Impl->DepthTarget.ViewHandle;
    DepthAtt.imageLayout                   = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    DepthAtt.loadOp                        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAtt.storeOp                       = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAtt.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo BeginSpec {};
    BeginSpec.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    BeginSpec.renderArea           = { { 0, 0 }, { WidthPx, HeightPx } };
    BeginSpec.layerCount           = 1u;
    BeginSpec.colorAttachmentCount = static_cast<uint32_t>(ColorAtts.size());
    BeginSpec.pColorAttachments    = ColorAtts.data();
    BeginSpec.pDepthAttachment     = &DepthAtt;

    VkViewport Viewport { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor  { { 0, 0 }, { WidthPx, HeightPx } };

    auto BindRasterProgram = [&](VkPipeline Pipe)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
        vkCmdSetViewport(Cmd, 0u, 1u, &Viewport);
        vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->PipeLayout, 0u, 1u, &Impl->DescSet, 0u, nullptr);
    };

    auto RecordSpan = [&](VkBuffer Vb, uint32_t VertCount, const ComponentSurfacePushConstantsGpu& Push)
    {
        if (Vb == VK_NULL_HANDLE || VertCount == 0u) return;
        const VkDeviceSize Offset = 0ull;
        vkCmdBindVertexBuffers(Cmd, 0u, 1u, &Vb, &Offset);
        vkCmdPushConstants(
            Cmd,
            Impl->PipeLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0u,
            sizeof(Push),
            &Push);
        vkCmdDraw(Cmd, VertCount, 1u, 0u, 0u);
    };

    Impl->DynBeginRender(Cmd, &BeginSpec);

    // 1. Ground dot floor
    if (Options.ShowDotFloor && !Options.TransparentBackdrop && Impl->DotFloorBuf.VertexCount > 0u)
    {
        BindRasterProgram(Impl->DotFloorPipe);
        ComponentSurfacePushConstantsGpu FloorPush {};
        const Mat4 Id = Mat4Identity();
        std::memcpy(FloorPush.WorldTransform, Id.M, sizeof(FloorPush.WorldTransform));
        RecordSpan(Impl->DotFloorBuf.Buffer.BufferHandle, Impl->DotFloorBuf.VertexCount, FloorPush);
    }

    // 2. Compiled GeometryInstances (Layer 0)
    const VkPipeline SurfacePipe = (Options.Channel == ComponentInspectionChannel::Collision && Impl->ChannelAlphaPipe != VK_NULL_HANDLE)
        ? Impl->ChannelAlphaPipe
        : Impl->ChannelOpaquePipe;
    BindRasterProgram(SurfacePipe);

    for (const auto* HPtr : SpatialOrdered)
    {
        const auto& H = *HPtr;
        if (H.Category != ComponentClass::ComponentCategory::GeometryInstance || !H.VisibilityEnabled)
        {
            continue;
        }
        const auto* Geom = ClassDef.Storage.InspectGeometry(H.Token);
        if (Geom == nullptr) continue;
        const auto CatIt = GpuCatalog.find(Geom->AssetIdentifier);
        if (CatIt == GpuCatalog.end() || CatIt->second == nullptr) continue;

        const CachedAssetGpuEntry* Rec = CatIt->second;
        const GpuVertexSliceUpload* VSlice = &Rec->FullBuf;
        if (Options.Channel == ComponentInspectionChannel::Density)
        {
            VSlice = &Rec->DensityBuf;
        }
        else if (Options.Channel == ComponentInspectionChannel::LodClusters)
        {
            VSlice = &Rec->LodBuf;
        }

        const bool Selected = (H.Token == Options.SelectedToken);
        ++Result.DrawnGeometries;
        Result.DrawnTriangles += Rec->Batch.TriangleCount;

        for (const auto& Slice : Rec->Batch.SlotSlices)
        {
            ComponentSurfacePushConstantsGpu Push {};
            std::memcpy(Push.WorldTransform, WorldMats[H.Token.SlotIndex].M, sizeof(Push.WorldTransform));

            Mathematics::Scalar3D Albedo = Slice.AlbedoLinearRGB;
            for (const auto& Ov : Geom->SlotOverrides)
            {
                if (Ov.SlotIdentifier == Slice.SlotIdentifier)
                {
                    Albedo = Ov.AlbedoLinearRGB;
                    break;
                }
            }
            Push.AlbedoRoughness[0]   = Albedo.X;
            Push.AlbedoRoughness[1]   = Albedo.Y;
            Push.AlbedoRoughness[2]   = Albedo.Z;
            Push.AlbedoRoughness[3]   = Slice.Roughness;
            Push.EmissiveMetalness[0] = Slice.EmissiveRGB.X;
            Push.EmissiveMetalness[1] = Slice.EmissiveRGB.Y;
            Push.EmissiveMetalness[2] = Slice.EmissiveRGB.Z;
            Push.EmissiveMetalness[3] = Slice.Metalness;
            Push.ChannelModifiers[0]  = Selected ? 1.0f : 0.0f;
            Push.ChannelModifiers[1]  = (Options.Channel == ComponentInspectionChannel::Collision) ? 0.35f : 1.0f;
            Push.ChannelModifiers[2]  = Rec->Batch.ValidUvsPresent ? 0.0f : 1.0f;
            Push.PickPacked[0]        = static_cast<uint32_t>(H.Category);
            Push.PickPacked[1]        = H.Token.SlotIndex;
            Push.PickPacked[2]        = H.Token.Generation;
            Push.PickPacked[3]        = static_cast<uint32_t>(Options.Channel);

            if (VSlice->Buffer.BufferHandle != VK_NULL_HANDLE && Slice.VertexCount > 0u)
            {
                const VkDeviceSize Offset = 0ull;
                vkCmdBindVertexBuffers(Cmd, 0u, 1u, &VSlice->Buffer.BufferHandle, &Offset);
                vkCmdPushConstants(
                    Cmd,
                    Impl->PipeLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0u,
                    sizeof(Push),
                    &Push);
                vkCmdDraw(Cmd, Slice.VertexCount, 1u, Slice.FirstVertex, 0u);
            }
        }
    }

    // 3. Field shells (Collision channel SDF/Box shells + CollisionVolumes)
    if (Options.ShowDebugHelpers)
    {
        BindRasterProgram(Impl->FieldHullRaster);

        if (Options.Channel == ComponentInspectionChannel::Collision)
        {
            for (const auto* HPtr : SpatialOrdered)
            {
                const auto& H = *HPtr;
                if (H.Category != ComponentClass::ComponentCategory::GeometryInstance || !H.VisibilityEnabled) continue;
                const auto* Geom = ClassDef.Storage.InspectGeometry(H.Token);
                if (Geom == nullptr || Geom->CollisionProfile == ComponentClass::GeometryCollisionProfile::NoCollision) continue;
                const auto CatIt = GpuCatalog.find(Geom->AssetIdentifier);
                if (CatIt == GpuCatalog.end()) continue;

                const auto& BMin = CatIt->second->Batch.BoundsMin;
                const auto& BMax = CatIt->second->Batch.BoundsMax;
                const Mathematics::Axis3D Center { 0.5f * (BMin.X + BMax.X), 0.5f * (BMin.Y + BMax.Y), 0.5f * (BMin.Z + BMax.Z) };
                const Mathematics::Scalar3D Half {
                    std::max(0.5f * (BMax.X - BMin.X), 0.05f),
                    std::max(0.5f * (BMax.Y - BMin.Y), 0.05f),
                    std::max(0.5f * (BMax.Z - BMin.Z), 0.05f)
                };
                const Mat4 BoxMat = Mat4Multiply(WorldMats[H.Token.SlotIndex], Mat4Multiply(Mat4Translation(Center), Mat4Scale(Half)));
                ComponentSurfacePushConstantsGpu HullPush {};
                std::memcpy(HullPush.WorldTransform, BoxMat.M, sizeof(HullPush.WorldTransform));
                HullPush.AlbedoRoughness[0]   = 0.275f; // #46d267 green collision shell
                HullPush.AlbedoRoughness[1]   = 0.824f;
                HullPush.AlbedoRoughness[2]   = 0.404f;
                HullPush.ChannelModifiers[1]  = 0.05f;
                HullPush.ChannelModifiers[2]  = 0.50f;
                HullPush.ChannelModifiers[3]  = 0.22f;
                HullPush.EmissiveMetalness[0] = 0.35f;
                HullPush.PickPacked[0]        = static_cast<uint32_t>(H.Category);
                HullPush.PickPacked[1]        = H.Token.SlotIndex;
                HullPush.PickPacked[2]        = H.Token.Generation;
                RecordSpan(Impl->UnitBoxBuf.Buffer.BufferHandle, Impl->UnitBoxBuf.VertexCount, HullPush);
            }
        }

        for (const auto* HPtr : SpatialOrdered)
        {
            const auto& H = *HPtr;
            if (!H.VisibilityEnabled) continue;
            const bool Selected = (H.Token == Options.SelectedToken);
            if (H.Category == ComponentClass::ComponentCategory::CollisionVolume &&
                (Selected || Options.Channel == ComponentInspectionChannel::Collision))
            {
                const auto* Col = ClassDef.Storage.InspectCollision(H.Token);
                if (Col == nullptr) continue;
                Mat4 ScaleMat = Mat4Scale(Col->HalfExtentsMeters);
                VkBuffer ShellVb = Impl->UnitBoxBuf.Buffer.BufferHandle;
                uint32_t ShellVc = Impl->UnitBoxBuf.VertexCount;
                if (Col->Form == ComponentClass::CollisionVolumeCategory::Sphere)
                {
                    ScaleMat = Mat4Scale({ Col->RadiusMeters, Col->RadiusMeters, Col->RadiusMeters });
                    ShellVb = Impl->UnitSphereBuf.Buffer.BufferHandle;
                    ShellVc = Impl->UnitSphereBuf.VertexCount;
                }
                const Mat4 M = Mat4Multiply(WorldMats[H.Token.SlotIndex], ScaleMat);
                ComponentSurfacePushConstantsGpu Push {};
                std::memcpy(Push.WorldTransform, M.M, sizeof(Push.WorldTransform));
                Push.AlbedoRoughness[0]   = 1.0f;
                Push.AlbedoRoughness[1]   = 0.698f;
                Push.AlbedoRoughness[2]   = 0.220f;
                Push.ChannelModifiers[1]  = Selected ? 0.09f : 0.04f;
                Push.ChannelModifiers[2]  = Selected ? 0.72f : 0.45f;
                Push.ChannelModifiers[3]  = 0.22f;
                Push.EmissiveMetalness[0] = 0.40f;
                Push.PickPacked[0]        = static_cast<uint32_t>(H.Category);
                Push.PickPacked[1]        = H.Token.SlotIndex;
                Push.PickPacked[2]        = H.Token.Generation;
                RecordSpan(ShellVb, ShellVc, Push);
            }
        }

        // 4. Light & Camera volumetric beams (additive)
        if (Impl->LightVolumePipe != VK_NULL_HANDLE)
        {
            BindRasterProgram(Impl->LightVolumePipe);
            for (const auto* HPtr : SpatialOrdered)
            {
                const auto& H = *HPtr;
                if (!H.VisibilityEnabled) continue;
                if (H.Category == ComponentClass::ComponentCategory::LightInstance)
                {
                    const auto* Lgt = ClassDef.Storage.InspectLight(H.Token);
                    if (Lgt == nullptr || Lgt->Form != ComponentClass::LightSourceCategory::SpotBeam) continue;
                    const float BeamLen = std::min(Lgt->AttenuationRangeMeters * 0.35f, 3.4f);
                    const float BeamRad = std::tan(Lgt->ConeAngleDegrees * DegToRad) * BeamLen;
                    auto ConeVerts = ComponentGizmoGeometry::BuildSpotBeamCone(BeamLen, BeamRad, 28u);
                    const VkBuffer ConeVb = Impl->UploadTransientVertices(ConeVerts);
                    ComponentSurfacePushConstantsGpu Push {};
                    std::memcpy(Push.WorldTransform, WorldMats[H.Token.SlotIndex].M, sizeof(Push.WorldTransform));
                    Push.AlbedoRoughness[0]  = Lgt->LinearColourRGB.X;
                    Push.AlbedoRoughness[1]  = Lgt->LinearColourRGB.Y;
                    Push.AlbedoRoughness[2]  = Lgt->LinearColourRGB.Z;
                    Push.ChannelModifiers[1] = (H.Token == Options.SelectedToken) ? 0.34f : 0.22f;
                    Push.ChannelModifiers[3] = BeamLen;
                    RecordSpan(ConeVb, static_cast<uint32_t>(ConeVerts.size()), Push);
                }
                else if (H.Category == ComponentClass::ComponentCategory::CameraInstance &&
                         (!Camera.PilotActive || Camera.PilotCameraToken != H.Token))
                {
                    ComponentSurfacePushConstantsGpu Push {};
                    std::memcpy(Push.WorldTransform, WorldMats[H.Token.SlotIndex].M, sizeof(Push.WorldTransform));
                    Push.AlbedoRoughness[0]  = 0.56f;
                    Push.AlbedoRoughness[1]  = 0.72f;
                    Push.AlbedoRoughness[2]  = 1.0f;
                    Push.ChannelModifiers[1] = (H.Token == Options.SelectedToken) ? 0.36f : 0.16f;
                    Push.ChannelModifiers[3] = 1.8f;
                    RecordSpan(Impl->CameraFrustumBuf.Buffer.BufferHandle, Impl->CameraFrustumBuf.VertexCount, Push);
                }
            }
        }

        // 5. DirectionArrow + SpatialOrigin helpers
        BindRasterProgram(Impl->ConstraintSolidPipe);
        for (const auto* HPtr : SpatialOrdered)
        {
            const auto& H = *HPtr;
            if (!H.VisibilityEnabled) continue;
            if (H.Category == ComponentClass::ComponentCategory::SpatialOrigin)
            {
                ComponentSurfacePushConstantsGpu Push {};
                std::memcpy(Push.WorldTransform, WorldMats[H.Token.SlotIndex].M, sizeof(Push.WorldTransform));
                Push.AlbedoRoughness[0] = 0.85f; Push.AlbedoRoughness[1] = 0.85f; Push.AlbedoRoughness[2] = 0.88f; Push.AlbedoRoughness[3] = 0.35f;
                Push.EmissiveMetalness[0] = 0.2f; Push.EmissiveMetalness[1] = 0.2f; Push.EmissiveMetalness[2] = 0.22f; Push.EmissiveMetalness[3] = 0.2f;
                Push.ChannelModifiers[1] = 1.0f;
                Push.PickPacked[0] = static_cast<uint32_t>(H.Category); Push.PickPacked[1] = H.Token.SlotIndex; Push.PickPacked[2] = H.Token.Generation;
                RecordSpan(Impl->OriginRingBuf.Buffer.BufferHandle, Impl->OriginRingBuf.VertexCount, Push);
            }
            else if (H.Category == ComponentClass::ComponentCategory::DirectionArrow)
            {
                const auto* Arr = ClassDef.Storage.InspectArrow(H.Token);
                if (Arr == nullptr) continue;
                const Mat4 M = Mat4Multiply(WorldMats[H.Token.SlotIndex], Mat4Scale({ Arr->LengthMeters, Arr->LengthMeters, Arr->LengthMeters }));
                ComponentSurfacePushConstantsGpu Push {};
                std::memcpy(Push.WorldTransform, M.M, sizeof(Push.WorldTransform));
                Push.AlbedoRoughness[0] = Arr->LinearColourRGB.X; Push.AlbedoRoughness[1] = Arr->LinearColourRGB.Y; Push.AlbedoRoughness[2] = Arr->LinearColourRGB.Z; Push.AlbedoRoughness[3] = 0.35f;
                Push.EmissiveMetalness[0] = Arr->LinearColourRGB.X * 0.45f; Push.EmissiveMetalness[1] = Arr->LinearColourRGB.Y * 0.45f; Push.EmissiveMetalness[2] = Arr->LinearColourRGB.Z * 0.45f;
                Push.ChannelModifiers[1] = 1.0f;
                Push.PickPacked[0] = static_cast<uint32_t>(H.Category); Push.PickPacked[1] = H.Token.SlotIndex; Push.PickPacked[2] = H.Token.Generation;
                RecordSpan(Impl->ArrowUnitBuf.Buffer.BufferHandle, Impl->ArrowUnitBuf.VertexCount, Push);
            }
        }
    }

    auto ConstructConstraintVisuals = [&](bool OnlySelected)
    {
        for (const auto* HPtr : SpatialOrdered)
        {
            const auto& H = *HPtr;
            if (H.Category != ComponentClass::ComponentCategory::ConstraintInstance || !H.VisibilityEnabled) continue;
            const bool Selected = (H.Token == Options.SelectedToken);
            if (Selected != OnlySelected) continue;
            const auto* Con = ClassDef.Storage.InspectConstraint(H.Token);
            if (Con == nullptr) continue;

            const float CurCoord = Options.LiveConstraintCoordinates.contains(H.Token.SlotIndex)
                ? Options.LiveConstraintCoordinates.at(H.Token.SlotIndex)
                : 0.0f;
            const bool DockIntact = Options.DockIntactMask.contains(H.Token.SlotIndex)
                ? Options.DockIntactMask.at(H.Token.SlotIndex)
                : Con->DockConnected;

            const auto Pieces = ComponentGizmoGeometry::BuildConstraintVisualPieces(
                *Con, CurCoord, DockIntact, true, Options.ElapsedSeconds);
            for (const auto& Piece : Pieces)
            {
                const VkBuffer PieceVb = Impl->UploadTransientVertices(Piece.Vertices);
                if (PieceVb == VK_NULL_HANDLE) continue;
                BindRasterProgram(
                    Piece.PieceCategory == ConstraintPieceCategory::ShadedSolid
                        ? Impl->ConstraintSolidPipe
                        : Impl->ConstraintAdditivePipe);
                ComponentSurfacePushConstantsGpu Push {};
                std::memcpy(Push.WorldTransform, AnchorMats[H.Token.SlotIndex].M, sizeof(Push.WorldTransform));
                Push.AlbedoRoughness[0]   = Piece.AlbedoRgb[0];
                Push.AlbedoRoughness[1]   = Piece.AlbedoRgb[1];
                Push.AlbedoRoughness[2]   = Piece.AlbedoRgb[2];
                Push.AlbedoRoughness[3]   = Piece.Roughness;
                Push.EmissiveMetalness[0] = Piece.EmissiveRgb[0];
                Push.EmissiveMetalness[1] = Piece.EmissiveRgb[1];
                Push.EmissiveMetalness[2] = Piece.EmissiveRgb[2];
                Push.EmissiveMetalness[3] = Piece.Metalness;
                Push.ChannelModifiers[0]  = Selected ? 1.0f : 0.0f;
                Push.ChannelModifiers[1]  = Piece.AlphaOrScale;
                Push.ChannelModifiers[2]  = static_cast<float>(Piece.PieceCategory);
                Push.ChannelModifiers[3]  = Piece.NormCursor;
                Push.PickPacked[0]        = static_cast<uint32_t>(H.Category);
                Push.PickPacked[1]        = H.Token.SlotIndex;
                Push.PickPacked[2]        = H.Token.Generation;
                RecordSpan(PieceVb, static_cast<uint32_t>(Piece.Vertices.size()), Push);
            }
        }
    };

    if (Options.ShowDebugHelpers)
    {
        ConstructConstraintVisuals(false);
    }

    Impl->DynEndRender(Cmd);

    // Layer 1 X-ray pass: clear depth and draw selected ConstraintInstance right through occluding body/tyre geometry
    const auto* SelectedHdr = Options.SelectedToken.Valid() ? ClassDef.Storage.InspectHeader(Options.SelectedToken) : nullptr;
    if (Options.ShowDebugHelpers && SelectedHdr != nullptr && SelectedHdr->Category == ComponentClass::ComponentCategory::ConstraintInstance)
    {
        ColorAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ColorAtts[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        DepthAtt.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Impl->DynBeginRender(Cmd, &BeginSpec);
        ConstructConstraintVisuals(true);
        Impl->DynEndRender(Cmd);
    }

    if (PerformHostReadback)
    {
        Impl->TransitionImage(
            Cmd, Impl->ColorTarget.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        Impl->TransitionImage(
            Cmd, Impl->PickTarget.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy CopyRegion {};
        CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        CopyRegion.imageSubresource.layerCount = 1u;
        CopyRegion.imageExtent                 = { WidthPx, HeightPx, 1u };
        vkCmdCopyImageToBuffer(Cmd, Impl->ColorTarget.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Impl->ColorReadback.BufferHandle, 1u, &CopyRegion);
        vkCmdCopyImageToBuffer(Cmd, Impl->PickTarget.ImageHandle,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Impl->PickReadback.BufferHandle,  1u, &CopyRegion);
    }
    else
    {
        Impl->TransitionImage(
            Cmd, Impl->ColorTarget.ImageHandle, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    const uint64_t Tick = Impl->DeviceInterchange->DispatchCommandBufferAsync(Cmd);
    (void)Impl->DeviceInterchange->AwaitTimelineTick(Tick);
    Impl->DeviceInterchange->FreeCommandBuffer(Cmd);
    Impl->ReleaseTransientBuffers();

    Result.WidthPx         = WidthPx;
    Result.HeightPx        = HeightPx;
    Result.VulkanGpuActive = true;
    Result.ColorImageView  = Impl->ColorTarget.ViewHandle;

    if (PerformHostReadback)
    {
        const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
        if (Impl->ColorReadback.HostAddress != nullptr)
        {
            Result.DisplayRgba8.resize(PixelCount * 4u);
            std::memcpy(Result.DisplayRgba8.data(), Impl->ColorReadback.HostAddress, PixelCount * 4u);
        }
        if (Impl->PickReadback.HostAddress != nullptr)
        {
            Impl->CpuPickRgba32ui.resize(PixelCount * 4u);
            std::memcpy(Impl->CpuPickRgba32ui.data(), Impl->PickReadback.HostAddress, PixelCount * 16u);
        }
    }

    const auto EndClock = std::chrono::steady_clock::now();
    Result.RenderMillisec = std::chrono::duration<float, std::milli>(EndClock - StartClock).count();
    return Result;
}

ComponentClass::ComponentInstanceToken ComponentViewportRenderer::QueryPickAtPixel(
    const ComponentClass::ComponentClassDefinition& ClassDef,
    uint32_t                                        PixelX,
    uint32_t                                        PixelY) const noexcept
{
    if (Impl == nullptr || Impl->TargetW == 0u || Impl->TargetH == 0u ||
        PixelX >= Impl->TargetW || PixelY >= Impl->TargetH ||
        Impl->CpuPickRgba32ui.empty())
    {
        return {};
    }
    const std::size_t Offset = (static_cast<std::size_t>(PixelY) * Impl->TargetW + PixelX) * 4u;
    if (Offset + 2u >= Impl->CpuPickRgba32ui.size())
    {
        return {};
    }
    const uint32_t SlotIndex  = Impl->CpuPickRgba32ui[Offset + 1u];
    const uint32_t Generation = Impl->CpuPickRgba32ui[Offset + 2u];
    if (SlotIndex == 0u || Generation == 0u)
    {
        return {};
    }
    const ComponentClass::ComponentInstanceToken Candidate { SlotIndex, Generation };
    return (ClassDef.Storage.InspectHeader(Candidate) != nullptr) ? Candidate : ComponentClass::ComponentInstanceToken{};
}

bool ComponentViewportRenderer::WritePngFile(
    const std::filesystem::path& FilePath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     Rgba8Pixels)
{
    if (WidthPx == 0u || HeightPx == 0u || Rgba8Pixels.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    if (FilePath.has_parent_path())
    {
        std::error_code Ec {};
        std::filesystem::create_directories(FilePath.parent_path(), Ec);
    }
    return stbi_write_png(
        FilePath.string().c_str(),
        static_cast<int>(WidthPx),
        static_cast<int>(HeightPx),
        4,
        Rgba8Pixels.data(),
        static_cast<int>(WidthPx * 4u)) != 0;
}

} // namespace Frontier::GeometricRaster::ComponentEditor
