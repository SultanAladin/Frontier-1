/*============================================================================================================================================
                                                 VERTEXMOTIONATLASGPUBAKER.CPP
============================================================================================================================================*/
// 🧩 Phase G03 Implementation of VertexMotionAtlasGpuBaker (DeformableVatBake.comp.spv async compute + headless parity)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasGpuBaker.h"

#include <algorithm>
#include <cstring>

namespace Frontier::GeometrySpace::Deformation
{

VertexMotionAtlasGpuBaker::~VertexMotionAtlasGpuBaker()
{
    Teardown();
}

#if FRONTIER_VATBAKER_HAS_VULKAN
bool VertexMotionAtlasGpuBaker::AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept
{
    Interchange_ = interchange;
    return Interchange_ != nullptr && Interchange_->Ready();
}
#endif

bool VertexMotionAtlasGpuBaker::Initialize(const std::filesystem::path& compiledShaderDirectory)
{
    Teardown();
    Telemetry_ = {};

#if FRONTIER_VATBAKER_HAS_VULKAN
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        const auto spvPath = compiledShaderDirectory / "DeformableVatBake.comp.spv";
        if (std::filesystem::exists(spvPath) && Encoder_.InitializeEncoder(Interchange_, 64u))
        {
            const std::array<VkDescriptorType, 6> slots {
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
            };
            ProgVatBake_ = Encoder_.RegisterProgram(spvPath, slots, "CsVertexMotionAtlasBake");
            if (ProgVatBake_ != UINT32_MAX)
            {
                BufVatUbo_ = Encoder_.AllocateHostWritableBuffer(64ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
                GpuLiveReady_ = (BufVatUbo_.BufferHandle != VK_NULL_HANDLE);
            }
        }
    }
#else
    (void)compiledShaderDirectory;
#endif

    // Honest route reporting: never claim VulkanComputeGpu before real GPU dispatch executes.
    Telemetry_.Route             = VatBakeExecutionRoute::HeadlessCpuMirror;
    Telemetry_.RealGpuDispatched = false;
    Initialized_                 = true;
    return true;
}

void VertexMotionAtlasGpuBaker::Teardown() noexcept
{
#if FRONTIER_VATBAKER_HAS_VULKAN
    ReleaseGpuResources();
#endif
    GpuLiveReady_ = false;
    Initialized_  = false;
}

VertexMotionAtlas VertexMotionAtlasGpuBaker::Bake(
    SimulationSpace::Deformable::DeformableBody& body,
    const SimulationSpace::Deformable::DeformableSimulationConfiguration& configuration,
    const CageEmbeddedSurface& embeddedSurface,
    const VertexMotionAtlasBakeSettings& settings)
{
    if (!Initialized_)
    {
        (void)Initialize();
    }

    // Produce deterministic host representation (used for serialization/fallback & GPU upload seeding)
    VertexMotionAtlas baked = VertexMotionAtlasBaker::BakeFromDeformable(body, configuration, embeddedSurface, settings);
    Telemetry_.BakedSliceCount  = baked.SliceCount;
    Telemetry_.BakedVertexCount = baked.VertexCount;

#if FRONTIER_VATBAKER_HAS_VULKAN
    if (GpuLiveReady_ && Interchange_ != nullptr && Interchange_->Ready() && baked.Ready())
    {
        RetireCompletedCommandBuffersNonBlocking();
        const uint32_t vCount = baked.VertexCount;
        const uint32_t pCount = std::max(1u, body.PointCount);
        const uint32_t tCount = std::max(1u, body.TetCount);

        // Wait on any prior bake command buffer before reallocating shared bake buffers/images.
        uint64_t priorTick = 0ull;
        for (const auto& pending : InFlightCommands_)
        {
            if (pending.CmdBuffer != VK_NULL_HANDLE && pending.TimelineSignal > priorTick)
            {
                priorTick = pending.TimelineSignal;
            }
        }
        if (priorTick > 0ull)
        {
            (void)Interchange_->AwaitTimelineTick(priorTick);
            RetireCompletedCommandBuffersNonBlocking();
        }

        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        relBuf(BufCagePos_);
        relBuf(BufTetCofactors_);
        relBuf(BufBoundVerts_);
        if (ImgVatPosOut_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ImgVatPosOut_);
        if (ImgVatNrmOut_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ImgVatNrmOut_);

        // Matches Slang StructuredBuffer<CageBoundVertexGpu> std430 layout in DeformableLayouts.slang.
        struct alignas(16) CageBoundVertexGpuPod
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
        static_assert(sizeof(CageBoundVertexGpuPod) == 96u, "CageBoundVertexGpuPod std430 layout must be 96 bytes.");

        BufCagePos_      = Encoder_.AllocateHostWritableBuffer(static_cast<uint64_t>(pCount) * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufTetCofactors_ = Encoder_.AllocateHostWritableBuffer(static_cast<uint64_t>(tCount) * 48ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BufBoundVerts_   = Encoder_.AllocateHostWritableBuffer(
            static_cast<uint64_t>(std::max(1u, vCount)) * sizeof(CageBoundVertexGpuPod), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        // Upload real live cage positions + strain.
        if (BufCagePos_.HostAddress != nullptr)
        {
            auto* dst = static_cast<float*>(BufCagePos_.HostAddress);
            for (uint32_t i = 0u; i < pCount; ++i)
            {
                const bool hasPos = (static_cast<size_t>(i) * 3u + 2u) < body.Positions.size();
                dst[i * 4u + 0u] = hasPos ? body.Positions[i * 3u + 0u] : 0.0f;
                dst[i * 4u + 1u] = hasPos ? body.Positions[i * 3u + 1u] : 0.0f;
                dst[i * 4u + 2u] = hasPos ? body.Positions[i * 3u + 2u] : 0.0f;
                dst[i * 4u + 3u] = (i < body.PointStrain.size()) ? body.PointStrain[i] : 0.0f;
            }
        }

        // Upload real Nanson tet cofactor columns (3 x float4 per tet, default identity if headless/quad cage).
        if (BufTetCofactors_.HostAddress != nullptr)
        {
            auto* dst = static_cast<float*>(BufTetCofactors_.HostAddress);
            for (uint32_t t = 0u; t < tCount; ++t)
            {
                const bool hasCof = (static_cast<size_t>(t) * 9u + 8u) < body.TetCofactorNormals.size();
                if (hasCof)
                {
                    const float* c = &body.TetCofactorNormals[t * 9u];
                    dst[t * 12u + 0u] = c[0]; dst[t * 12u + 1u] = c[1]; dst[t * 12u + 2u] = c[2]; dst[t * 12u + 3u] = 0.0f;
                    dst[t * 12u + 4u] = c[3]; dst[t * 12u + 5u] = c[4]; dst[t * 12u + 6u] = c[5]; dst[t * 12u + 7u] = 0.0f;
                    dst[t * 12u + 8u] = c[6]; dst[t * 12u + 9u] = c[7]; dst[t * 12u + 10u] = c[8]; dst[t * 12u + 11u] = 0.0f;
                }
                else
                {
                    std::memset(dst + t * 12u, 0, 12u * sizeof(float));
                    dst[t * 12u + 0u] = 1.0f;
                    dst[t * 12u + 5u] = 1.0f;
                    dst[t * 12u + 10u] = 1.0f;
                }
            }
        }

        // Upload real cage-bound vertices.
        if (BufBoundVerts_.HostAddress != nullptr)
        {
            auto* dst = static_cast<CageBoundVertexGpuPod*>(BufBoundVerts_.HostAddress);
            for (uint32_t v = 0u; v < vCount; ++v)
            {
                CageBoundVertexGpuPod rec {};
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
                for (uint32_t k = 0u; k < 4u; ++k)
                {
                    const size_t idx = static_cast<size_t>(v) * 4u + k;
                    const uint32_t rawCage = (idx < embeddedSurface.CageIndices.size()) ? embeddedSurface.CageIndices[idx] : 0u;
                    rec.CageIndices[k] = std::min(rawCage, pCount - 1u);
                    rec.CageWeights[k] = (idx < embeddedSurface.CageWeights.size()) ? embeddedSurface.CageWeights[idx] : (k == 0u ? 1.0f : 0.0f);
                }
                if (static_cast<size_t>(v) * 4u + 3u < embeddedSurface.ExtraParams.size())
                {
                    if (embeddedSurface.Mode == CageEmbedMode::Tetrahedral3D)
                    {
                        const uint32_t tetIdx = static_cast<uint32_t>(std::max(0.0f, embeddedSurface.ExtraParams[v * 4u + 0u]));
                        rec.HostTetIndex = std::min(tetIdx, tCount - 1u);
                    }
                    else
                    {
                        rec.LocalBasisOffset[0] = embeddedSurface.ExtraParams[v * 4u + 0u];
                        rec.LocalBasisOffset[1] = embeddedSurface.ExtraParams[v * 4u + 1u];
                        rec.LocalBasisOffset[2] = embeddedSurface.ExtraParams[v * 4u + 3u];
                    }
                }
                dst[v] = rec;
            }
        }

        const VkImageUsageFlags vatUsage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ImgVatPosOut_ = Interchange_->AllocateImage2D(
            baked.AtlasWidth, baked.AtlasHeight, VK_FORMAT_R32G32B32A32_SFLOAT, vatUsage, VK_IMAGE_ASPECT_COLOR_BIT);
        ImgVatNrmOut_ = Interchange_->AllocateImage2D(
            baked.AtlasWidth, baked.AtlasHeight, VK_FORMAT_R32G32B32A32_SFLOAT, vatUsage, VK_IMAGE_ASPECT_COLOR_BIT);

        if (ImgVatPosOut_.ImageHandle != VK_NULL_HANDLE && ImgVatNrmOut_.ImageHandle != VK_NULL_HANDLE)
        {
            struct VatUniformPod
            {
                uint32_t PackingSlices[4]; // x = AtlasWidth, y = RowsPerSlice, z = targetSlice, w = slice1
                float    BlendFlags[4];    // x = sliceFrac, y = deformMode (1=TetCage, 2=QuadCage), z = heatmap, w = 0
                float    BoundsMinPad[4];
                float    BoundsMaxPad[4];
            } u {};
            u.PackingSlices[0] = baked.AtlasWidth;
            u.PackingSlices[1] = std::max(1u, baked.RowsPerSlice);
            u.PackingSlices[2] = 0u;
            u.PackingSlices[3] = 0u;
            u.BlendFlags[0]    = 0.0f;
            u.BlendFlags[1]    = (embeddedSurface.Mode == CageEmbedMode::Tetrahedral3D) ? 1.0f : 2.0f;

            VkCommandBuffer cmd = Interchange_->BeginSingleTimeCommands();
            if (cmd != VK_NULL_HANDLE)
            {
                // Transition newly allocated VAT RGBA32F images from UNDEFINED -> GENERAL and clear to zero before bake.
                VkImageSubresourceRange subRange {};
                subRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                subRange.baseMipLevel   = 0u;
                subRange.levelCount     = 1u;
                subRange.baseArrayLayer = 0u;
                subRange.layerCount     = 1u;

                std::array<VkImageMemoryBarrier, 2> barriers {};
                const std::array<VkImage, 2> imgs { ImgVatPosOut_.ImageHandle, ImgVatNrmOut_.ImageHandle };
                for (size_t i = 0u; i < 2u; ++i)
                {
                    barriers[i].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barriers[i].srcAccessMask       = 0u;
                    barriers[i].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barriers[i].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                    barriers[i].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
                    barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barriers[i].image               = imgs[i];
                    barriers[i].subresourceRange    = subRange;
                }
                vkCmdPipelineBarrier(
                    cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0u, 0u, nullptr, 0u, nullptr, 2u, barriers.data());

                VkClearColorValue zeroColor {};
                vkCmdClearColorImage(cmd, ImgVatPosOut_.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, &zeroColor, 1u, &subRange);
                vkCmdClearColorImage(cmd, ImgVatNrmOut_.ImageHandle, VK_IMAGE_LAYOUT_GENERAL, &zeroColor, 1u, &subRange);

                VkMemoryBarrier postClear {};
                postClear.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                postClear.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                postClear.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(
                    cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u, 1u, &postClear, 0u, nullptr, 0u, nullptr);

                if (BufVatUbo_.HostAddress != nullptr) std::memcpy(BufVatUbo_.HostAddress, &u, sizeof(u));
                const std::array<GeometricRaster::ComputeSlotDescriptor, 6> slots {{
                    { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BufVatUbo_.BufferHandle,       sizeof(u) },
                    { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufCagePos_.BufferHandle,      VK_WHOLE_SIZE },
                    { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufTetCofactors_.BufferHandle, VK_WHOLE_SIZE },
                    { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BufBoundVerts_.BufferHandle,   VK_WHOLE_SIZE },
                    { 4u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVatPosOut_.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
                    { 5u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE, 0ull, 0ull, ImgVatNrmOut_.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL }
                }};
                const uint32_t gx = (std::max(1u, vCount) + 63u) / 64u;
                if (Encoder_.EncodeComputeDispatch(cmd, ProgVatBake_, slots, gx))
                {
                    const uint64_t signal = Interchange_->DispatchCommandBufferAsync(cmd);
                    const uint32_t ringSlot = ActiveSlot_;
                    ActiveSlot_ = (ActiveSlot_ + 1u) % kTripleBufferSlots;
                    InFlightCommands_[ringSlot].CmdBuffer      = cmd;
                    InFlightCommands_[ringSlot].TimelineSignal = signal;
                    Telemetry_.TotalGpuDispatches   += 1ull;
                    Telemetry_.LastSubmittedTimeline = signal;
                    Telemetry_.AllocatedImageBytes   =
                        static_cast<uint64_t>(baked.AtlasWidth) * baked.AtlasHeight * 16ull * 2ull;
                    Telemetry_.Route                 = VatBakeExecutionRoute::VulkanComputeGpu;
                    Telemetry_.RealGpuDispatched     = true;
                    return baked;
                }
                Interchange_->FreeCommandBuffer(cmd);
            }
        }
    }
#endif

    Telemetry_.Route             = VatBakeExecutionRoute::HeadlessCpuMirror;
    Telemetry_.RealGpuDispatched = false;
    return baked;
}

#if FRONTIER_VATBAKER_HAS_VULKAN
void VertexMotionAtlasGpuBaker::RetireCompletedCommandBuffersNonBlocking() noexcept
{
    if (Interchange_ == nullptr || !Interchange_->Ready()) return;
    for (auto& pending : InFlightCommands_)
    {
        if (pending.CmdBuffer != VK_NULL_HANDLE &&
            Interchange_->QueryTimelineReached(pending.TimelineSignal))
        {
            Interchange_->FreeCommandBuffer(pending.CmdBuffer);
            pending.CmdBuffer      = VK_NULL_HANDLE;
            pending.TimelineSignal = 0ull;
        }
    }
}

void VertexMotionAtlasGpuBaker::ReleaseGpuResources() noexcept
{
    if (Interchange_ != nullptr && Interchange_->Ready())
    {
        RetireCompletedCommandBuffersNonBlocking();
        uint64_t maxPendingTick = 0ull;
        for (const auto& pending : InFlightCommands_)
        {
            if (pending.CmdBuffer != VK_NULL_HANDLE && pending.TimelineSignal > maxPendingTick)
            {
                maxPendingTick = pending.TimelineSignal;
            }
        }
        if (maxPendingTick > 0ull)
        {
            // Teardown-only synchronization so in-flight GPU command buffers complete before destroying resources.
            (void)Interchange_->AwaitTimelineTick(maxPendingTick);
        }
        for (auto& pending : InFlightCommands_)
        {
            if (pending.CmdBuffer != VK_NULL_HANDLE)
            {
                Interchange_->FreeCommandBuffer(pending.CmdBuffer);
                pending = {};
            }
        }
        auto relBuf = [&](GeometricRaster::DeviceBufferResource& b)
        {
            if (b.BufferHandle != VK_NULL_HANDLE) Interchange_->ReleaseBuffer(b);
        };
        relBuf(BufVatUbo_);
        relBuf(BufCagePos_);
        relBuf(BufTetCofactors_);
        relBuf(BufBoundVerts_);
        if (ImgVatPosOut_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ImgVatPosOut_);
        if (ImgVatNrmOut_.ImageHandle != VK_NULL_HANDLE) Interchange_->ReleaseImage2D(ImgVatNrmOut_);
    }
    Encoder_.TeardownEncoder();
    ProgVatBake_ = UINT32_MAX;
    GpuLiveReady_ = false;
}
#endif

} // namespace Frontier::GeometrySpace::Deformation
