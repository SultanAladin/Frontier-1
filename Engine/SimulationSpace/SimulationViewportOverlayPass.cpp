/*============================================================================================================================================
                                                SIMULATIONVIEWPORTOVERLAYPASS.CPP
============================================================================================================================================*/
// 🧩 Implementation of SimulationViewportOverlayPass: Real Vulkan 1.2 viewport overlay consuming SimulationGpuRenderFeedPacket
//    (zero CPU readback during live rendering) across Deformable meshes, 3D Fluid volumes, and Particle clouds.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationViewportOverlayPass.h"

#include "Engine/SimulationSpace/Fluid/FluidRegionTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::SimulationSpace
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  MATH & UNIFORM HELPERS
//------------------------------------------------------------------------------------------------------------------------

struct Mat4RowMajor
{
    float M[4][4] {};
};

std::array<float, 3> Sub3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

float Dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> Cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

std::array<float, 3> Norm3(const std::array<float, 3>& v, const std::array<float, 3>& fallback) noexcept
{
    const float len = std::sqrt(Dot3(v, v));
    if (len < 1.0e-6f) return fallback;
    const float inv = 1.0f / len;
    return { v[0] * inv, v[1] * inv, v[2] * inv };
}

Mat4RowMajor MulMat4(const Mat4RowMajor& a, const Mat4RowMajor& b) noexcept
{
    Mat4RowMajor r {};
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            r.M[i][j] = a.M[i][0] * b.M[0][j] + a.M[i][1] * b.M[1][j]
                      + a.M[i][2] * b.M[2][j] + a.M[i][3] * b.M[3][j];
        }
    }
    return r;
}

bool InvertMat4(const Mat4RowMajor& inM, Mat4RowMajor& outInv) noexcept
{
    float m[16] {};
    std::memcpy(m, &inM.M[0][0], sizeof(m));
    float inv[16] {};

    inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    inv[2]  =  m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6]  = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] =  m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    inv[3]  = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7]  =  m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] =  m[0] * m[5] * m[10] - m[0] * m[6] * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1.0e-12f) return false;
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) inv[i] *= invDet;
    std::memcpy(&outInv.M[0][0], inv, sizeof(inv));
    return true;
}

void BuildVulkanViewProjection(
    const SimulationOverlayCamera& cam,
    float aspect,
    Mat4RowMajor& outViewProj,
    Mat4RowMajor& outInvViewProj,
    std::array<float, 3>& outForward,
    float& outFocalPixels,
    float viewportHeightPx) noexcept
{
    const auto f = Norm3(Sub3(cam.TargetMeters, cam.EyeMeters), { 0.0f, 0.0f, -1.0f });
    const auto r = Norm3(Cross3(f, cam.UpAxis), { 1.0f, 0.0f, 0.0f });
    const auto u = Norm3(Cross3(r, f), { 0.0f, 1.0f, 0.0f });
    outForward = f;

    // Right-handed view matrix (camera looks along -Z_view)
    Mat4RowMajor V {};
    V.M[0][0] =  r[0]; V.M[0][1] =  r[1]; V.M[0][2] =  r[2]; V.M[0][3] = -Dot3(r, cam.EyeMeters);
    V.M[1][0] =  u[0]; V.M[1][1] =  u[1]; V.M[1][2] =  u[2]; V.M[1][3] = -Dot3(u, cam.EyeMeters);
    V.M[2][0] = -f[0]; V.M[2][1] = -f[1]; V.M[2][2] = -f[2]; V.M[2][3] =  Dot3(f, cam.EyeMeters);
    V.M[3][3] =  1.0f;

    // Vulkan clip-space perspective (Y flipped downward, Z in [0, 1])
    const float tanHalf = std::tan(std::clamp(cam.VerticalFovRad, 0.1f, 2.5f) * 0.5f);
    const float fy = 1.0f / std::max(tanHalf, 1.0e-4f);
    const float fx = fy / std::max(aspect, 1.0e-3f);
    const float n  = std::max(cam.NearPlaneMeters, 1.0e-3f);
    const float farP = std::max(cam.FarPlaneMeters, n + 1.0f);

    Mat4RowMajor P {};
    P.M[0][0] = fx;
    P.M[1][1] = -fy;
    P.M[2][2] = farP / (n - farP);
    P.M[2][3] = (farP * n) / (n - farP);
    P.M[3][2] = -1.0f;

    outViewProj = MulMat4(P, V);
    (void)InvertMat4(outViewProj, outInvViewProj);
    outFocalPixels = 0.5f * viewportHeightPx * fy;
}

// Matches FluidVolumeRaymarch.slang byte-for-byte (400 bytes).
struct alignas(16) FluidVolumeRaymarchUniformPod
{
    Fluid::FluidVolumeUniformBlock Volume;                  // 160 B
    Mat4RowMajor                   InverseViewProjection;   //  64 B
    std::array<float, 4>           CameraPositionFireLight; // xyz = cam, w = fireLight
    std::array<float, 4>           ViewportGroundExposure;  // xy = vp, z = groundY, w = exposure
    std::array<float, 4>           LightDirectionIntensity; // xyz = sun dir, w = intensity
    std::array<float, 4>           LightColourAnisotropy;   // xyz = sun RGB, w = g
    std::array<float, 4>           AlbedoDensityScale;      // xyz = albedo, w = densityScale
    std::array<float, 4>           AmbientColourAbsorption; // xyz = ambient, w = absorption
    std::array<float, 4>           FireEmberIntensity;      // xyz = ember RGB, w = fireIntensity
    std::array<float, 4>           FireFlameThreshold;      // xyz = flame RGB, w = fireThreshold
    std::array<float, 4>           FireCoreSoftness;        // xyz = core RGB, w = fireSoftness
    std::array<float, 4>           FireOpacitySteps;        // x = fireOpacity, y = steps, z = shadowSteps, w = showVolume
    std::array<int32_t, 4>         FlagsSelection;          // x = transparent, y = voxelPreview, z = tick, w = selectedCollider
};
static_assert(sizeof(FluidVolumeRaymarchUniformPod) == 400u, "FluidVolumeRaymarchUniformPod must be 400 bytes.");

// Matches ParticleSpriteRasterization.slang byte-for-byte (512 bytes).
struct alignas(16) ParticleSpriteUniformPod
{
    // ParticleShapeUniformBlock (11 * float4 = 176 B)
    std::array<float, 4> OriginRadius;
    std::array<float, 4> AxisFill;
    std::array<float, 4> ShapeCounts;
    std::array<float, 4> AnchorLayout;
    std::array<float, 4> MocapParams;
    std::array<float, 4> MocapKickEffector;
    std::array<float, 4> EffectorMotion;
    std::array<float, 4> EffectorAxisDerez;
    std::array<float, 4> DerezFrontParams;
    std::array<float, 4> DerezEdgeColourWing;
    std::array<float, 4> GlitchParams;
    // ParticleShadeUniformBlock (12 * float4 = 192 B)
    std::array<std::array<float, 4>, 8> SwatchStops;
    std::array<float, 4> ModeRampSwatch;
    std::array<float, 4> ColourJitterCycle;
    std::array<float, 4> LifeRangeFlicker;
    std::array<float, 4> TimingRegionHeight;
    // Camera & sprite parameters (64 + 5 * 16 = 144 B)
    Mat4RowMajor         ViewProjection;
    std::array<float, 4> CameraPositionFocal;
    std::array<float, 4> CameraForwardViewportX;
    std::array<float, 4> ViewportYSizeParams;
    std::array<float, 4> PixelDofParams;
    std::array<float, 4> HaloSpriteStyleTrail;
};
static_assert(sizeof(ParticleSpriteUniformPod) == 512u, "ParticleSpriteUniformPod must be 512 bytes.");

// Matches DeformableLayouts.slang VertexMotionAtlasUniforms (64 bytes).
struct alignas(16) DeformableVatUniformPod
{
    std::array<uint32_t, 4> PackingSlices;
    std::array<float, 4>    BlendFlags;
    std::array<float, 4>    BoundsMinPad;
    std::array<float, 4>    BoundsMaxPad;
};
static_assert(sizeof(DeformableVatUniformPod) == 64u, "DeformableVatUniformPod must be 64 bytes.");

// Matches TireCarcassMaterial.slang MaterialCameraUniforms (96 bytes).
struct alignas(16) DeformableCameraUniformPod
{
    Mat4RowMajor         ViewProj;
    std::array<float, 4> CameraPosition;
    std::array<float, 4> KeyLightDir;
};
static_assert(sizeof(DeformableCameraUniformPod) == 96u, "DeformableCameraUniformPod must be 96 bytes.");

// Matches DeformableLayouts.slang CageBoundVertexGpu std430 layout (96 bytes).
struct alignas(16) DeformableBoundVertexGpuPod
{
    float    RestPosition[3];
    float    Pad0;
    float    RestNormal[3];
    float    Pad1;
    float    TexCoord[2];
    float    ZoneId;
    float    Pad2;
    uint32_t CageIndices[4];
    float    CageWeights[4];
    float    LocalBasisOffset[3];
    uint32_t HostTetIndex;
};
static_assert(sizeof(DeformableBoundVertexGpuPod) == 96u, "DeformableBoundVertexGpuPod must be 96 bytes.");

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

SimulationViewportOverlayPass::~SimulationViewportOverlayPass()
{
    TeardownOverlay();
}

#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
void SimulationViewportOverlayPass::RetireCompletedCommandBuffersNonBlocking() noexcept
{
    if (Interchange_ == nullptr || !Interchange_->Ready()) return;
    for (auto& pending : InFlightCommands_)
    {
        if (pending.CmdBuffer != VK_NULL_HANDLE &&
            Interchange_->QueryTimelineReached(pending.TimelineSignal))
        {
            Interchange_->FreeCommandBuffer(pending.CmdBuffer);
            pending = {};
        }
    }
    auto it = OverflowPending_.begin();
    while (it != OverflowPending_.end())
    {
        if (it->CmdBuffer != VK_NULL_HANDLE &&
            Interchange_->QueryTimelineReached(it->TimelineSignal))
        {
            Interchange_->FreeCommandBuffer(it->CmdBuffer);
            it = OverflowPending_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SimulationViewportOverlayPass::DestroyPipelineUnit(GraphicsPipelineUnit& unit) noexcept
{
    if (Interchange_ == nullptr || !Interchange_->Ready()) return;
    VkDevice dev = Interchange_->AccessDevice();
    if (unit.Pipeline != VK_NULL_HANDLE)       vkDestroyPipeline(dev, unit.Pipeline, nullptr);
    if (unit.PipeLayout != VK_NULL_HANDLE)     vkDestroyPipelineLayout(dev, unit.PipeLayout, nullptr);
    if (unit.SetLayout != VK_NULL_HANDLE)      vkDestroyDescriptorSetLayout(dev, unit.SetLayout, nullptr);
    if (unit.VertModule != VK_NULL_HANDLE)     vkDestroyShaderModule(dev, unit.VertModule, nullptr);
    if (unit.FragModule != VK_NULL_HANDLE)     vkDestroyShaderModule(dev, unit.FragModule, nullptr);
    unit = {};
}

void SimulationViewportOverlayPass::ReleaseRenderTargets() noexcept
{
    if (Interchange_ == nullptr || !Interchange_->Ready()) return;
    VkDevice dev = Interchange_->AccessDevice();
    if (Framebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(dev, Framebuffer_, nullptr);
        Framebuffer_ = VK_NULL_HANDLE;
    }
    if (ColorTarget_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ColorTarget_);
    if (DepthTarget_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(DepthTarget_);
    TargetWidth_  = 0u;
    TargetHeight_ = 0u;
}

bool SimulationViewportOverlayPass::EnsureRenderTargets(uint32_t widthPx, uint32_t heightPx)
{
    if (widthPx == 0u || heightPx == 0u || Interchange_ == nullptr || !Interchange_->Ready()) return false;
    if (ColorTarget_.ImageHandle != VK_NULL_HANDLE && TargetWidth_ == widthPx && TargetHeight_ == heightPx)
    {
        return true;
    }

    // Wait for any in-flight command buffers targeting old framebuffers before resizing.
    uint64_t maxPending = 0ull;
    for (const auto& p : InFlightCommands_)
    {
        if (p.CmdBuffer != VK_NULL_HANDLE && p.TimelineSignal > maxPending) maxPending = p.TimelineSignal;
    }
    if (maxPending > 0ull)
    {
        (void)Interchange_->AwaitTimelineTick(maxPending);
        RetireCompletedCommandBuffersNonBlocking();
    }

    ReleaseRenderTargets();

    ColorTarget_ = Interchange_->AllocateImage2D(
        widthPx, heightPx, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    DepthTarget_ = Interchange_->AllocateImage2D(
        widthPx, heightPx, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    if (ColorTarget_.ImageHandle == VK_NULL_HANDLE || DepthTarget_.ImageHandle == VK_NULL_HANDLE)
    {
        ReleaseRenderTargets();
        return false;
    }

    const std::array<VkImageView, 2> views { ColorTarget_.ViewHandle, DepthTarget_.ViewHandle };
    VkFramebufferCreateInfo fbCi {};
    fbCi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCi.renderPass      = RenderPass_;
    fbCi.attachmentCount = static_cast<uint32_t>(views.size());
    fbCi.pAttachments    = views.data();
    fbCi.width           = widthPx;
    fbCi.height          = heightPx;
    fbCi.layers          = 1u;
    if (vkCreateFramebuffer(Interchange_->AccessDevice(), &fbCi, nullptr, &Framebuffer_) != VK_SUCCESS)
    {
        ReleaseRenderTargets();
        return false;
    }

    TargetWidth_  = widthPx;
    TargetHeight_ = heightPx;
    ++ImageGeneration_;
    return true;
}

bool SimulationViewportOverlayPass::BuildBlitPipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv)
{
    VkDevice dev = Interchange_->AccessDevice();
    BlitPipe_.VertModule = Interchange_->LoadShaderBinary(vertSpv);
    BlitPipe_.FragModule = Interchange_->LoadShaderBinary(fragSpv);
    if (BlitPipe_.VertModule == VK_NULL_HANDLE || BlitPipe_.FragModule == VK_NULL_HANDLE) return false;

    const VkDescriptorSetLayoutBinding b { 0u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo slCi {};
    slCi.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slCi.bindingCount = 1u;
    slCi.pBindings    = &b;
    if (vkCreateDescriptorSetLayout(dev, &slCi, nullptr, &BlitPipe_.SetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo plCi {};
    plCi.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1u;
    plCi.pSetLayouts    = &BlitPipe_.SetLayout;
    if (vkCreatePipelineLayout(dev, &plCi, nullptr, &BlitPipe_.PipeLayout) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = BlitPipe_.VertModule;
    stages[0].pName  = "VertexEntry";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = BlitPipe_.FragModule;
    stages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo vi {}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia {}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp {}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1u;
    vp.scissorCount  = 1u;
    VkPipelineRasterizationStateCreateInfo rs {}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms {}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds {}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState ba {};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb {}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1u;
    cb.pAttachments    = &ba;
    const std::array<VkDynamicState, 2> dyn { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy {}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dy.pDynamicStates    = dyn.data();

    VkGraphicsPipelineCreateInfo gp {}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2u;
    gp.pStages             = stages.data();
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dy;
    gp.layout              = BlitPipe_.PipeLayout;
    gp.renderPass          = RenderPass_;
    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1u, &gp, nullptr, &BlitPipe_.Pipeline) == VK_SUCCESS;
}

bool SimulationViewportOverlayPass::BuildFluidPipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv)
{
    VkDevice dev = Interchange_->AccessDevice();
    FluidPipe_.VertModule = Interchange_->LoadShaderBinary(vertSpv);
    FluidPipe_.FragModule = Interchange_->LoadShaderBinary(fragSpv);
    if (FluidPipe_.VertModule == VK_NULL_HANDLE || FluidPipe_.FragModule == VK_NULL_HANDLE) return false;

    const std::array<VkDescriptorSetLayoutBinding, 4> bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_SAMPLER,        1u, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    }};
    VkDescriptorSetLayoutCreateInfo slCi {}; slCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slCi.bindingCount = static_cast<uint32_t>(bindings.size());
    slCi.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &slCi, nullptr, &FluidPipe_.SetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo plCi {}; plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1u;
    plCi.pSetLayouts    = &FluidPipe_.SetLayout;
    if (vkCreatePipelineLayout(dev, &plCi, nullptr, &FluidPipe_.PipeLayout) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = FluidPipe_.VertModule;
    stages[0].pName  = "FullscreenVertexEntry";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = FluidPipe_.FragModule;
    stages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo vi {}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia {}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp {}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1u;
    vp.scissorCount  = 1u;
    VkPipelineRasterizationStateCreateInfo rs {}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms {}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds {}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState ba {};
    ba.blendEnable         = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp        = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp        = VK_BLEND_OP_ADD;
    ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb {}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1u;
    cb.pAttachments    = &ba;
    const std::array<VkDynamicState, 2> dyn { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy {}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dy.pDynamicStates    = dyn.data();

    VkGraphicsPipelineCreateInfo gp {}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2u;
    gp.pStages             = stages.data();
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dy;
    gp.layout              = FluidPipe_.PipeLayout;
    gp.renderPass          = RenderPass_;
    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1u, &gp, nullptr, &FluidPipe_.Pipeline) == VK_SUCCESS;
}

bool SimulationViewportOverlayPass::BuildParticlePipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv)
{
    VkDevice dev = Interchange_->AccessDevice();
    ParticlePipe_.VertModule = Interchange_->LoadShaderBinary(vertSpv);
    ParticlePipe_.FragModule = Interchange_->LoadShaderBinary(fragSpv);
    if (ParticlePipe_.VertModule == VK_NULL_HANDLE || ParticlePipe_.FragModule == VK_NULL_HANDLE) return false;

    const std::array<VkDescriptorSetLayoutBinding, 4> bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr }
    }};
    VkDescriptorSetLayoutCreateInfo slCi {}; slCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slCi.bindingCount = static_cast<uint32_t>(bindings.size());
    slCi.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &slCi, nullptr, &ParticlePipe_.SetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo plCi {}; plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1u;
    plCi.pSetLayouts    = &ParticlePipe_.SetLayout;
    if (vkCreatePipelineLayout(dev, &plCi, nullptr, &ParticlePipe_.PipeLayout) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = ParticlePipe_.VertModule;
    stages[0].pName  = "ParticleSpriteVertexEntry";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ParticlePipe_.FragModule;
    stages[1].pName  = "ParticleSpriteFragmentEntry";

    VkPipelineVertexInputStateCreateInfo vi {}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia {}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp {}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1u;
    vp.scissorCount  = 1u;
    VkPipelineRasterizationStateCreateInfo rs {}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms {}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds {}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba {};
    ba.blendEnable         = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp        = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp        = VK_BLEND_OP_ADD;
    ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb {}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1u;
    cb.pAttachments    = &ba;
    const std::array<VkDynamicState, 2> dyn { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy {}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dy.pDynamicStates    = dyn.data();

    VkGraphicsPipelineCreateInfo gp {}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2u;
    gp.pStages             = stages.data();
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dy;
    gp.layout              = ParticlePipe_.PipeLayout;
    gp.renderPass          = RenderPass_;
    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1u, &gp, nullptr, &ParticlePipe_.Pipeline) == VK_SUCCESS;
}

bool SimulationViewportOverlayPass::BuildDeformablePipeline(const std::filesystem::path& vertSpv, const std::filesystem::path& fragSpv)
{
    VkDevice dev = Interchange_->AccessDevice();
    DeformablePipe_.VertModule = Interchange_->LoadShaderBinary(vertSpv);
    DeformablePipe_.FragModule = Interchange_->LoadShaderBinary(fragSpv);
    if (DeformablePipe_.VertModule == VK_NULL_HANDLE || DeformablePipe_.FragModule == VK_NULL_HANDLE) return false;

    const std::array<VkDescriptorSetLayoutBinding, 5> bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
        { 4u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
    }};
    VkDescriptorSetLayoutCreateInfo slCi {}; slCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slCi.bindingCount = static_cast<uint32_t>(bindings.size());
    slCi.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &slCi, nullptr, &DeformablePipe_.SetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo plCi {}; plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.setLayoutCount = 1u;
    plCi.pSetLayouts    = &DeformablePipe_.SetLayout;
    if (vkCreatePipelineLayout(dev, &plCi, nullptr, &DeformablePipe_.PipeLayout) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = DeformablePipe_.VertModule;
    stages[0].pName  = "VsCageSkinning";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = DeformablePipe_.FragModule;
    stages[1].pName  = "FsTireCarcassMaterial";

    VkPipelineVertexInputStateCreateInfo vi {}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia {}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp {}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1u;
    vp.scissorCount  = 1u;
    VkPipelineRasterizationStateCreateInfo rs {}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms {}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds {}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba {};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb {}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1u;
    cb.pAttachments    = &ba;
    const std::array<VkDynamicState, 2> dyn { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy {}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dy.pDynamicStates    = dyn.data();

    VkGraphicsPipelineCreateInfo gp {}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2u;
    gp.pStages             = stages.data();
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dy;
    gp.layout              = DeformablePipe_.PipeLayout;
    gp.renderPass          = RenderPass_;
    return vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1u, &gp, nullptr, &DeformablePipe_.Pipeline) == VK_SUCCESS;
}

bool SimulationViewportOverlayPass::InitializeOverlay(
    GeometricRaster::VulkanInterchange* boundDevice,
    const std::filesystem::path&        compiledShaderDirectory)
{
    TeardownOverlay();
    if (boundDevice == nullptr || !boundDevice->Ready()) return false;
    Interchange_ = boundDevice;
    VkDevice dev = Interchange_->AccessDevice();

    // Render pass: Color RGBA8 (CLEAR -> SHADER_READ_ONLY_OPTIMAL) + Depth D32F (CLEAR -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    std::array<VkAttachmentDescription, 2> attachments {};
    attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef { 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1u;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0u;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCi {}; rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpCi.pAttachments    = attachments.data();
    rpCi.subpassCount    = 1u;
    rpCi.pSubpasses      = &subpass;
    rpCi.dependencyCount = 1u;
    rpCi.pDependencies   = &dep;
    if (vkCreateRenderPass(dev, &rpCi, nullptr, &RenderPass_) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    VkSamplerCreateInfo sampCi {}; sampCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCi.magFilter    = VK_FILTER_LINEAR;
    sampCi.minFilter    = VK_FILTER_LINEAR;
    sampCi.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(dev, &sampCi, nullptr, &LinearSampler_) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 5> poolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kCommandRingSlots * 8u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kCommandRingSlots * 12u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          kCommandRingSlots * 6u },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                kCommandRingSlots * 4u },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kCommandRingSlots * 4u }
    }};
    VkDescriptorPoolCreateInfo dpCi {}; dpCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCi.maxSets       = kCommandRingSlots * 8u;
    dpCi.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpCi.pPoolSizes    = poolSizes.data();
    if (vkCreateDescriptorPool(dev, &dpCi, nullptr, &DescriptorPool_) != VK_SUCCESS)
    {
        TeardownOverlay();
        return false;
    }

    const auto blitV = compiledShaderDirectory / "WorldViewportBlit.vert.spv";
    const auto blitF = compiledShaderDirectory / "WorldViewportBlit.frag.spv";
    if (std::filesystem::exists(blitV) && std::filesystem::exists(blitF))
    {
        (void)BuildBlitPipeline(blitV, blitF);
    }

    const auto fluidV = compiledShaderDirectory / "FluidVolumeRaymarch.vert.spv";
    const auto fluidF = compiledShaderDirectory / "FluidVolumeRaymarch.frag.spv";
    if (std::filesystem::exists(fluidV) && std::filesystem::exists(fluidF))
    {
        FluidPipelineReady_ = BuildFluidPipeline(fluidV, fluidF);
    }

    const auto partV = compiledShaderDirectory / "ParticleSpriteQuad.vert.spv";
    const auto partF = compiledShaderDirectory / "ParticleSprite.frag.spv";
    if (std::filesystem::exists(partV) && std::filesystem::exists(partF))
    {
        ParticlePipelineReady_ = BuildParticlePipeline(partV, partF);
    }

    const auto defV = compiledShaderDirectory / "DeformableCageSkinning.vert.spv";
    const auto defF = compiledShaderDirectory / "DeformableTireMaterial.frag.spv";
    if (std::filesystem::exists(defV) && std::filesystem::exists(defF))
    {
        DeformablePipelineReady_ = BuildDeformablePipeline(defV, defF);
    }

    if (!FluidPipelineReady_ && !ParticlePipelineReady_ && !DeformablePipelineReady_)
    {
        TeardownOverlay();
        return false;
    }

    auto allocHostBuf = [&](uint64_t bytes, VkBufferUsageFlags usage)
    {
        return Interchange_->AllocateBuffer(
            bytes, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    };

    FallbackParticleBuf_ = allocHostBuf(64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (FallbackParticleBuf_.HostAddress != nullptr) std::memset(FallbackParticleBuf_.HostAddress, 0, 64u);

    for (uint32_t s = 0u; s < kCommandRingSlots; ++s)
    {
        FluidUbo_[s]     = allocHostBuf(sizeof(FluidVolumeRaymarchUniformPod), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        ParticleUbo_[s]  = allocHostBuf(sizeof(ParticleSpriteUniformPod), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        DeformVatUbo_[s] = allocHostBuf(sizeof(DeformableVatUniformPod), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        DeformCamUbo_[s] = allocHostBuf(sizeof(DeformableCameraUniformPod), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        auto allocSet = [&](VkDescriptorSetLayout layout, VkDescriptorSet& outSet)
        {
            if (layout == VK_NULL_HANDLE) return;
            VkDescriptorSetAllocateInfo ai {}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool     = DescriptorPool_;
            ai.descriptorSetCount = 1u;
            ai.pSetLayouts        = &layout;
            (void)vkAllocateDescriptorSets(dev, &ai, &outSet);
        };
        allocSet(BlitPipe_.SetLayout,       BlitDescSets_[s]);
        allocSet(FluidPipe_.SetLayout,      FluidDescSets_[s]);
        allocSet(ParticlePipe_.SetLayout,   PartDescSets_[s]);
        allocSet(DeformablePipe_.SetLayout, DeformDescSets_[s]);
    }

    Ready_ = true;
    return true;
}
#endif

void SimulationViewportOverlayPass::TeardownOverlay() noexcept
{
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        RetireCompletedCommandBuffersNonBlocking();
        uint64_t maxPending = 0ull;
        for (const auto& p : InFlightCommands_)
        {
            if (p.CmdBuffer != VK_NULL_HANDLE && p.TimelineSignal > maxPending) maxPending = p.TimelineSignal;
        }
        for (const auto& p : OverflowPending_)
        {
            if (p.CmdBuffer != VK_NULL_HANDLE && p.TimelineSignal > maxPending) maxPending = p.TimelineSignal;
        }
        if (maxPending > 0ull)
        {
            // Teardown-only synchronization so in-flight graphics command buffers finish before resource destruction.
            (void)Interchange_->AwaitTimelineTick(maxPending);
        }
        for (auto& p : InFlightCommands_)
        {
            if (p.CmdBuffer != VK_NULL_HANDLE)
            {
                Interchange_->FreeCommandBuffer(p.CmdBuffer);
                p = {};
            }
        }
        for (auto& p : OverflowPending_)
        {
            if (p.CmdBuffer != VK_NULL_HANDLE)
            {
                Interchange_->FreeCommandBuffer(p.CmdBuffer);
            }
        }
        OverflowPending_.clear();

        ReleaseRenderTargets();
        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        for (uint32_t s = 0u; s < kCommandRingSlots; ++s)
        {
            relBuf(FluidUbo_[s]);
            relBuf(ParticleUbo_[s]);
            relBuf(DeformVatUbo_[s]);
            relBuf(DeformCamUbo_[s]);
        }
        relBuf(DeformBoundVerts_);
        relBuf(DeformIndices_);
        relBuf(FallbackParticleBuf_);

        DestroyPipelineUnit(BlitPipe_);
        DestroyPipelineUnit(FluidPipe_);
        DestroyPipelineUnit(ParticlePipe_);
        DestroyPipelineUnit(DeformablePipe_);

        VkDevice dev = Interchange_->AccessDevice();
        if (DescriptorPool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(dev, DescriptorPool_, nullptr);
            DescriptorPool_ = VK_NULL_HANDLE;
        }
        if (LinearSampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(dev, LinearSampler_, nullptr);
            LinearSampler_ = VK_NULL_HANDLE;
        }
        if (RenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(dev, RenderPass_, nullptr);
            RenderPass_ = VK_NULL_HANDLE;
        }
    }
    Interchange_ = nullptr;
    LastSubmittedTimeline_ = 0ull;
#endif
    Ready_                   = false;
    FluidPipelineReady_      = false;
    ParticlePipelineReady_   = false;
    DeformablePipelineReady_ = false;
    DeformableIndexCount_    = 0u;
    DeformableVertexCount_   = 0u;
}

bool SimulationViewportOverlayPass::UploadDeformableSurfaceMesh(
    const GeometrySpace::Deformation::CageEmbeddedSurface& embeddedSurface,
    uint32_t maxCagePointCount,
    uint32_t maxTetCount)
{
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    if (!Ready_ || Interchange_ == nullptr || !Interchange_->Ready()) return false;
    const uint32_t vCount = embeddedSurface.VertexCount();
    const uint32_t iCount = static_cast<uint32_t>(embeddedSurface.RestSurface.Indices.size());
    if (vCount == 0u || iCount < 3u) return false;

    uint64_t maxPending = 0ull;
    for (const auto& p : InFlightCommands_)
    {
        if (p.CmdBuffer != VK_NULL_HANDLE && p.TimelineSignal > maxPending) maxPending = p.TimelineSignal;
    }
    if (maxPending > 0ull)
    {
        (void)Interchange_->AwaitTimelineTick(maxPending);
        RetireCompletedCommandBuffersNonBlocking();
    }

    if (DeformBoundVerts_.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(DeformBoundVerts_);
    if (DeformIndices_.BufferHandle != VK_NULL_HANDLE)    Interchange_->ReleaseBuffer(DeformIndices_);

    const uint32_t safeCageMax = std::max(1u, maxCagePointCount);
    const uint32_t safeTetMax  = std::max(1u, maxTetCount);

    DeformBoundVerts_ = Interchange_->AllocateBuffer(
        static_cast<uint64_t>(vCount) * sizeof(DeformableBoundVertexGpuPod),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    DeformIndices_ = Interchange_->AllocateBuffer(
        static_cast<uint64_t>(iCount) * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (DeformBoundVerts_.HostAddress == nullptr || DeformIndices_.HostAddress == nullptr)
    {
        return false;
    }

    auto* dstVerts = static_cast<DeformableBoundVertexGpuPod*>(DeformBoundVerts_.HostAddress);
    for (uint32_t v = 0u; v < vCount; ++v)
    {
        DeformableBoundVertexGpuPod rec {};
        if (static_cast<size_t>(v) * 3u + 2u < embeddedSurface.RestSurface.RestPositions.size())
        {
            rec.RestPosition[0] = embeddedSurface.RestSurface.RestPositions[v * 3u + 0u];
            rec.RestPosition[1] = embeddedSurface.RestSurface.RestPositions[v * 3u + 1u];
            rec.RestPosition[2] = embeddedSurface.RestSurface.RestPositions[v * 3u + 2u];
        }
        if (static_cast<size_t>(v) * 3u + 2u < embeddedSurface.RestSurface.RestNormals.size())
        {
            rec.RestNormal[0] = embeddedSurface.RestSurface.RestNormals[v * 3u + 0u];
            rec.RestNormal[1] = embeddedSurface.RestSurface.RestNormals[v * 3u + 1u];
            rec.RestNormal[2] = embeddedSurface.RestSurface.RestNormals[v * 3u + 2u];
        }
        else
        {
            rec.RestNormal[1] = 1.0f;
        }
        rec.ZoneId = (v < embeddedSurface.GpuVertices.size()) ? embeddedSurface.GpuVertices[v].MaterialPbr[1] : 0.0f;
        for (uint32_t k = 0u; k < 4u; ++k)
        {
            const size_t idx = static_cast<size_t>(v) * 4u + k;
            const uint32_t rawCage = (idx < embeddedSurface.CageIndices.size()) ? embeddedSurface.CageIndices[idx] : 0u;
            rec.CageIndices[k] = std::min(rawCage, safeCageMax - 1u);
            rec.CageWeights[k] = (idx < embeddedSurface.CageWeights.size()) ? embeddedSurface.CageWeights[idx] : (k == 0u ? 1.0f : 0.0f);
        }
        if (static_cast<size_t>(v) * 4u + 3u < embeddedSurface.ExtraParams.size())
        {
            if (embeddedSurface.Mode == GeometrySpace::Deformation::CageEmbedMode::Tetrahedral3D)
            {
                const uint32_t tetIdx = static_cast<uint32_t>(std::max(0.0f, embeddedSurface.ExtraParams[v * 4u + 0u]));
                rec.HostTetIndex = std::min(tetIdx, safeTetMax - 1u);
            }
            else
            {
                rec.LocalBasisOffset[0] = embeddedSurface.ExtraParams[v * 4u + 0u];
                rec.LocalBasisOffset[1] = embeddedSurface.ExtraParams[v * 4u + 1u];
                rec.LocalBasisOffset[2] = embeddedSurface.ExtraParams[v * 4u + 3u];
            }
        }
        dstVerts[v] = rec;
    }

    std::memcpy(DeformIndices_.HostAddress, embeddedSurface.RestSurface.Indices.data(),
                static_cast<size_t>(iCount) * sizeof(uint32_t));
    DeformableVertexCount_ = vCount;
    DeformableIndexCount_  = iCount;
    DeformableEmbedMode_   = (embeddedSurface.Mode == GeometrySpace::Deformation::CageEmbedMode::Tetrahedral3D) ? 1u : 2u;
    return true;
#else
    (void)embeddedSurface;
    (void)maxCagePointCount;
    (void)maxTetCount;
    return false;
#endif
}

bool SimulationViewportOverlayPass::CompositeOntoGpuViewport(
    const SimulationViewportCompositeRequest& request,
    SimulationViewportCompositeResult&        outResult)
{
    outResult = {};
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    if (!Ready_ || Interchange_ == nullptr || !Interchange_->Ready()) return false;
    if (!EnsureRenderTargets(request.TargetWidthPx, request.TargetHeightPx)) return false;

    RetireCompletedCommandBuffersNonBlocking();

    const uint32_t slot = ActiveSlot_;
    // Triple-buffer ring guard: only wait if CPU wrapped 3 full frames ahead of GPU on this slot.
    if (InFlightCommands_[slot].CmdBuffer != VK_NULL_HANDLE)
    {
        if (InFlightCommands_[slot].TimelineSignal > 0ull)
        {
            (void)Interchange_->AwaitTimelineTick(InFlightCommands_[slot].TimelineSignal);
        }
        Interchange_->FreeCommandBuffer(InFlightCommands_[slot].CmdBuffer);
        InFlightCommands_[slot] = {};
    }
    VkDevice dev = Interchange_->AccessDevice();
    const float wF = static_cast<float>(TargetWidth_);
    const float hF = static_cast<float>(TargetHeight_);
    const float aspect = wF / std::max(1.0f, hF);

    Mat4RowMajor viewProj {};
    Mat4RowMajor invViewProj {};
    std::array<float, 3> camForward { 0.0f, 0.0f, -1.0f };
    float focalPixels = hF;
    BuildVulkanViewProjection(request.Camera, aspect, viewProj, invViewProj, camForward, focalPixels, hF);

    const bool hasWorldSource = (request.WorldSourceView != VK_NULL_HANDLE && BlitPipe_.Pipeline != VK_NULL_HANDLE);
    const bool drawFluid = request.DrawFluidVolume && FluidPipelineReady_ &&
                           request.RenderFeed.FluidVolume.GpuResident &&
                           request.RenderFeed.FluidVolume.StateViewHandle != 0ull &&
                           request.RenderFeed.FluidVolume.PressureViewHandle != 0ull;
    const bool drawDeform = request.DrawDeformables && DeformablePipelineReady_ &&
                            DeformableIndexCount_ >= 3u &&
                            request.RenderFeed.HeroDeformable.GpuResident &&
                            request.RenderFeed.HeroDeformable.PositionBufferHandle != 0ull &&
                            request.RenderFeed.HeroDeformable.CofactorBufferHandle != 0ull;
    const bool drawParticles = request.DrawParticles && ParticlePipelineReady_ &&
                               request.RenderFeed.ParticleCloud.GpuResident &&
                               request.RenderFeed.ParticleCloud.ActiveParticleCount > 0u &&
                               request.RenderFeed.ParticleCloud.StateBufferHandle != 0ull;

    // 1. Update Blit descriptor set if compositing over live World viewport texture
    if (hasWorldSource)
    {
        VkDescriptorImageInfo imgInfo {};
        imgInfo.sampler     = LinearSampler_;
        imgInfo.imageView   = request.WorldSourceView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w {}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = BlitDescSets_[slot];
        w.dstBinding      = 0u;
        w.descriptorCount = 1u;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(dev, 1u, &w, 0u, nullptr);
    }

    // 2. Update Fluid UBO + descriptors
    if (drawFluid)
    {
        FluidVolumeRaymarchUniformPod fu {};
        fu.Volume.GridBoundary = {
            static_cast<int32_t>(request.RenderFeed.FluidVolume.GridCells[0]),
            static_cast<int32_t>(request.RenderFeed.FluidVolume.GridCells[1]),
            static_cast<int32_t>(request.RenderFeed.FluidVolume.GridCells[2]),
            0
        };
        fu.Volume.GridCentreCellSize = {
            request.RenderFeed.FluidVolume.CentreMeters[0],
            request.RenderFeed.FluidVolume.CentreMeters[1],
            request.RenderFeed.FluidVolume.CentreMeters[2],
            request.RenderFeed.FluidVolume.CellSizeMeters
        };
        fu.InverseViewProjection   = invViewProj;
        fu.CameraPositionFireLight = { request.Camera.EyeMeters[0], request.Camera.EyeMeters[1], request.Camera.EyeMeters[2], 1.6f };
        fu.ViewportGroundExposure  = { wF, hF, request.GroundHeightMeters, request.Exposure };
        fu.LightDirectionIntensity = { 0.48f, 0.78f, 0.40f, 3.2f };
        fu.LightColourAnisotropy   = { 1.00f, 0.96f, 0.90f, 0.52f };
        fu.AlbedoDensityScale      = { 0.78f, 0.80f, 0.84f, 2.2f };
        fu.AmbientColourAbsorption = { 0.24f, 0.29f, 0.38f, 1.35f };
        fu.FireEmberIntensity      = { 0.95f, 0.22f, 0.03f, 2.6f };
        fu.FireFlameThreshold      = { 1.00f, 0.56f, 0.12f, 0.45f };
        fu.FireCoreSoftness        = { 1.00f, 0.94f, 0.76f, 0.42f };
        fu.FireOpacitySteps        = { 1.4f, 72.0f, 12.0f, 1.0f };
        fu.FlagsSelection          = { hasWorldSource ? 1 : 0, 0, static_cast<int32_t>(request.RenderFeed.CoordinatorTickIndex), -1 };

        if (FluidUbo_[slot].HostAddress != nullptr) std::memcpy(FluidUbo_[slot].HostAddress, &fu, sizeof(fu));

        VkDescriptorBufferInfo uboInfo { FluidUbo_[slot].BufferHandle, 0ull, sizeof(fu) };
        VkDescriptorImageInfo stateInfo { VK_NULL_HANDLE, reinterpret_cast<VkImageView>(request.RenderFeed.FluidVolume.StateViewHandle), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo pressInfo { VK_NULL_HANDLE, reinterpret_cast<VkImageView>(request.RenderFeed.FluidVolume.PressureViewHandle), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo sampInfo  { LinearSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };

        std::array<VkWriteDescriptorSet, 4> ws {};
        ws[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FluidDescSets_[slot], 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr };
        ws[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FluidDescSets_[slot], 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  &stateInfo, nullptr, nullptr };
        ws[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FluidDescSets_[slot], 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  &pressInfo, nullptr, nullptr };
        ws[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FluidDescSets_[slot], 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_SAMPLER,        &sampInfo, nullptr, nullptr };
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(ws.size()), ws.data(), 0u, nullptr);
    }

    // 3. Update Deformable UBO + descriptors (zero CPU readback — binds live compute SSBO handles!)
    if (drawDeform)
    {
        DeformableVatUniformPod vatU {};
        vatU.PackingSlices = { 256u, 1u, 0u, 0u };
        vatU.BlendFlags    = { 0.0f, static_cast<float>(DeformableEmbedMode_), request.ShowStrainHeatmap ? 1.0f : 0.0f, 0.0f };
        if (DeformVatUbo_[slot].HostAddress != nullptr) std::memcpy(DeformVatUbo_[slot].HostAddress, &vatU, sizeof(vatU));

        DeformableCameraUniformPod camU {};
        camU.ViewProj       = viewProj;
        camU.CameraPosition = { request.Camera.EyeMeters[0], request.Camera.EyeMeters[1], request.Camera.EyeMeters[2], 1.0f };
        camU.KeyLightDir    = { 0.45f, 0.80f, 0.38f, 0.0f };
        if (DeformCamUbo_[slot].HostAddress != nullptr) std::memcpy(DeformCamUbo_[slot].HostAddress, &camU, sizeof(camU));

        VkDescriptorBufferInfo vatInfo  { DeformVatUbo_[slot].BufferHandle, 0ull, sizeof(vatU) };
        VkDescriptorBufferInfo posInfo  { reinterpret_cast<VkBuffer>(request.RenderFeed.HeroDeformable.PositionBufferHandle), 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo cofInfo  { reinterpret_cast<VkBuffer>(request.RenderFeed.HeroDeformable.CofactorBufferHandle), 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo bndInfo  { DeformBoundVerts_.BufferHandle, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo camInfo  { DeformCamUbo_[slot].BufferHandle, 0ull, sizeof(camU) };

        std::array<VkWriteDescriptorSet, 5> ws {};
        ws[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DeformDescSets_[slot], 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &vatInfo, nullptr };
        ws[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DeformDescSets_[slot], 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &posInfo, nullptr };
        ws[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DeformDescSets_[slot], 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cofInfo, nullptr };
        ws[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DeformDescSets_[slot], 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bndInfo, nullptr };
        ws[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DeformDescSets_[slot], 4u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &camInfo, nullptr };
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(ws.size()), ws.data(), 0u, nullptr);
    }

    // 4. Update Particle Sprite UBO + descriptors (zero CPU readback — binds live compute SSBO handles!)
    const uint32_t drawParticleCount = drawParticles
        ? std::min(request.RenderFeed.ParticleCloud.ActiveParticleCount, request.RenderFeed.ParticleCloud.MaxParticleCapacity)
        : 0u;
    if (drawParticles && drawParticleCount > 0u)
    {
        ParticleSpriteUniformPod pu {};
        pu.OriginRadius       = { 0.0f, 1.0f, 0.0f, 1.0f };
        pu.AxisFill           = { 0.0f, 1.0f, 0.0f, 1.0f };
        pu.ShapeCounts        = { 0.0f, static_cast<float>(drawParticleCount), 1.0f, 0.0f };
        pu.AnchorLayout       = { 1.0f, 1.0f, static_cast<float>(std::max(1u, drawParticleCount)), 0.0f };
        pu.SwatchStops[0]     = { request.ParticleColorStart[0], request.ParticleColorStart[1], request.ParticleColorStart[2], 1.0f };
        pu.SwatchStops[1]     = { request.ParticleColorEnd[0],   request.ParticleColorEnd[1],   request.ParticleColorEnd[2],   1.0f };
        pu.ModeRampSwatch     = { 0.0f, 0.0f, 0.0f, 2.0f };
        pu.ColourJitterCycle  = { 0.04f, 0.08f, 0.0f, 1.35f };
        pu.LifeRangeFlicker   = { 4.0f, 0.0f, 12.0f, 6.0f };
        pu.TimingRegionHeight = { 0.0f, static_cast<float>(request.RenderFeed.SimulationSeconds), 8.0f, 0.0f };
        pu.ViewProjection     = viewProj;
        pu.CameraPositionFocal    = { request.Camera.EyeMeters[0], request.Camera.EyeMeters[1], request.Camera.EyeMeters[2], focalPixels };
        pu.CameraForwardViewportX = { camForward[0], camForward[1], camForward[2], wF };
        pu.ViewportYSizeParams    = { hF, request.ParticleSizeMeters, request.ParticleSizeMeters * 0.45f, 0.25f };
        pu.PixelDofParams         = { 1.0f, 0.0f, 8.0f, 0.0f };
        pu.HaloSpriteStyleTrail   = { 1.0f, 0.95f, 0.0f, 0.0f };

        if (ParticleUbo_[slot].HostAddress != nullptr) std::memcpy(ParticleUbo_[slot].HostAddress, &pu, sizeof(pu));

        const VkBuffer prevBuf = (request.RenderFeed.ParticleCloud.PrevPosBufferHandle != 0ull)
            ? reinterpret_cast<VkBuffer>(request.RenderFeed.ParticleCloud.PrevPosBufferHandle)
            : FallbackParticleBuf_.BufferHandle;
        const VkBuffer anchBuf = (request.RenderFeed.ParticleCloud.AnchorBufferHandle != 0ull)
            ? reinterpret_cast<VkBuffer>(request.RenderFeed.ParticleCloud.AnchorBufferHandle)
            : FallbackParticleBuf_.BufferHandle;

        VkDescriptorBufferInfo uboInfo  { ParticleUbo_[slot].BufferHandle, 0ull, sizeof(pu) };
        VkDescriptorBufferInfo stInfo   { reinterpret_cast<VkBuffer>(request.RenderFeed.ParticleCloud.StateBufferHandle), 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo prevInfo { prevBuf, 0ull, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo anchInfo { anchBuf, 0ull, VK_WHOLE_SIZE };

        std::array<VkWriteDescriptorSet, 4> ws {};
        ws[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, PartDescSets_[slot], 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr };
        ws[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, PartDescSets_[slot], 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &stInfo,  nullptr };
        ws[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, PartDescSets_[slot], 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &prevInfo, nullptr };
        ws[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, PartDescSets_[slot], 3u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &anchInfo, nullptr };
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(ws.size()), ws.data(), 0u, nullptr);
    }

    VkCommandBuffer cmd = Interchange_->BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    // Single-queue FIFO compute-to-graphics visibility barrier for live simulation SSBOs & 3D volumes
    if (request.RenderFeed.QueueSync.RequiresComputeToGraphicsSync)
    {
        VkMemoryBarrier mb {}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0u, 1u, &mb, 0u, nullptr, 0u, nullptr);
    }

    std::array<VkClearValue, 2> clears {};
    clears[0].color        = { { 0.055f, 0.060f, 0.072f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo rpBi {}; rpBi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBi.renderPass        = RenderPass_;
    rpBi.framebuffer       = Framebuffer_;
    rpBi.renderArea.extent = { TargetWidth_, TargetHeight_ };
    rpBi.clearValueCount   = static_cast<uint32_t>(clears.size());
    rpBi.pClearValues      = clears.data();
    vkCmdBeginRenderPass(cmd, &rpBi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp { 0.0f, 0.0f, wF, hF, 0.0f, 1.0f };
    VkRect2D   sc { { 0, 0 }, { TargetWidth_, TargetHeight_ } };
    vkCmdSetViewport(cmd, 0u, 1u, &vp);
    vkCmdSetScissor(cmd, 0u, 1u, &sc);

    // Pass A: Blit live 3D World viewport source if provided
    if (hasWorldSource)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, BlitPipe_.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, BlitPipe_.PipeLayout, 0u, 1u, &BlitDescSets_[slot], 0u, nullptr);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    }

    // Pass B: When standalone (no WorldSourceView), draw Fluid first so it lays down the 3D studio horizon + perspective
    // ground grid + volume shadow, then draw depth-tested Deformable meshes, then Particles. When compositing over a
    // live WorldSourceView, draw opaque Deformables first, then transparent Fluid volume, then Particles!
    auto emitDeformableDraw = [&]()
    {
        if (!drawDeform) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, DeformablePipe_.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, DeformablePipe_.PipeLayout, 0u, 1u, &DeformDescSets_[slot], 0u, nullptr);
        vkCmdBindIndexBuffer(cmd, DeformIndices_.BufferHandle, 0ull, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, DeformableIndexCount_, 1u, 0u, 0, 0u);
        outResult.DeformablePassesDrawn = 1u;
    };
    auto emitFluidDraw = [&]()
    {
        if (!drawFluid) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FluidPipe_.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FluidPipe_.PipeLayout, 0u, 1u, &FluidDescSets_[slot], 0u, nullptr);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        outResult.FluidPassesDrawn = 1u;
    };
    auto emitParticleDraw = [&]()
    {
        if (!drawParticles || drawParticleCount == 0u) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ParticlePipe_.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ParticlePipe_.PipeLayout, 0u, 1u, &PartDescSets_[slot], 0u, nullptr);
        vkCmdDraw(cmd, drawParticleCount * 6u, 1u, 0u, 0u);
        outResult.ParticlePassesDrawn = 1u;
    };

    if (hasWorldSource)
    {
        emitDeformableDraw();
        emitFluidDraw();
        emitParticleDraw();
    }
    else
    {
        emitFluidDraw();
        emitDeformableDraw();
        emitParticleDraw();
    }

    vkCmdEndRenderPass(cmd);

    const uint64_t signal = Interchange_->DispatchCommandBufferAsync(cmd);
    if (InFlightCommands_[slot].CmdBuffer != VK_NULL_HANDLE)
    {
        if (Interchange_->QueryTimelineReached(InFlightCommands_[slot].TimelineSignal))
        {
            Interchange_->FreeCommandBuffer(InFlightCommands_[slot].CmdBuffer);
        }
        else
        {
            // Strictly non-blocking: park until completed and retire on a future tick or teardown.
            OverflowPending_.push_back(InFlightCommands_[slot]);
        }
    }
    InFlightCommands_[slot].CmdBuffer      = cmd;
    InFlightCommands_[slot].TimelineSignal = signal;
    LastSubmittedTimeline_                 = signal;
    ActiveSlot_ = (ActiveSlot_ + 1u) % kCommandRingSlots;

    outResult.ColorImageView    = ColorTarget_.ViewHandle;
    outResult.ImageGeneration   = ImageGeneration_;
    outResult.SubmittedTimeline = signal;
    outResult.WidthPx           = TargetWidth_;
    outResult.HeightPx          = TargetHeight_;
    return true;
#else
    (void)request;
    return false;
#endif
}

bool SimulationViewportOverlayPass::ReadbackLastCompositeRgba8(
    std::vector<uint8_t>& outRgba8,
    uint32_t&             outWidthPx,
    uint32_t&             outHeightPx)
{
    outRgba8.clear();
    outWidthPx  = 0u;
    outHeightPx = 0u;
#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    if (!Ready_ || Interchange_ == nullptr || !Interchange_->Ready() ||
        ColorTarget_.ImageHandle == VK_NULL_HANDLE || TargetWidth_ == 0u || TargetHeight_ == 0u)
    {
        return false;
    }

    if (LastSubmittedTimeline_ > 0ull)
    {
        (void)Interchange_->AwaitTimelineTick(LastSubmittedTimeline_);
    }

    const uint64_t totalBytes = static_cast<uint64_t>(TargetWidth_) * TargetHeight_ * 4ull;
    auto staging = Interchange_->AllocateBuffer(
        totalBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (staging.BufferHandle == VK_NULL_HANDLE || staging.HostAddress == nullptr)
    {
        if (staging.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(staging);
        return false;
    }

    VkCommandBuffer cmd = Interchange_->BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE)
    {
        Interchange_->ReleaseBuffer(staging);
        return false;
    }

    VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    VkImageMemoryBarrier toSrc {}; toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = ColorTarget_.ImageHandle;
    toSrc.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &toSrc);

    VkBufferImageCopy region {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    region.imageExtent      = { TargetWidth_, TargetHeight_, 1u };
    vkCmdCopyImageToBuffer(cmd, ColorTarget_.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.BufferHandle, 1u, &region);

    VkImageMemoryBarrier backRead {}; backRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    backRead.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    backRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    backRead.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    backRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backRead.image               = ColorTarget_.ImageHandle;
    backRead.subresourceRange    = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, nullptr, 0u, nullptr, 1u, &backRead);

    const uint64_t copyTick = Interchange_->DispatchCommandBufferAsync(cmd);
    (void)Interchange_->AwaitTimelineTick(copyTick);
    Interchange_->FreeCommandBuffer(cmd);

    outRgba8.resize(static_cast<size_t>(totalBytes));
    std::memcpy(outRgba8.data(), staging.HostAddress, static_cast<size_t>(totalBytes));
    outWidthPx  = TargetWidth_;
    outHeightPx = TargetHeight_;

    Interchange_->ReleaseBuffer(staging);
    return true;
#else
    return false;
#endif
}

} // namespace Frontier::SimulationSpace
