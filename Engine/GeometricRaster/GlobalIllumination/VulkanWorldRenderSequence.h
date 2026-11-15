/*============================================================================================================================================
                                                         VULKANWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Hardware Vulkan .slang compute sequence executing Tier 1 GTX & Tier 2 RTX World Viewport rendering on the GPU

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                           VULKAN WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class VulkanWorldRenderSequence
{
public:
    VulkanWorldRenderSequence() = default;
    ~VulkanWorldRenderSequence();

    VulkanWorldRenderSequence(const VulkanWorldRenderSequence&)            = delete;
    VulkanWorldRenderSequence& operator=(const VulkanWorldRenderSequence&) = delete;

    [[nodiscard]] bool InitializeSequence(
        VulkanInterchange&           TargetDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownSequence() noexcept;

    [[nodiscard]] bool             QueryReady() const noexcept;
    [[nodiscard]] bool             QueryHardwareGpuActive() const noexcept;
    [[nodiscard]] std::string_view QueryAdapterTitle() const noexcept;
    [[nodiscard]] float            QueryLastDispatchMilliseconds() const noexcept { return LastDispatchMilliseconds; }

    void SynchronizeSceneResources(
        const WorldSceneSpecification&   Scene,
        const IrradianceLatticeSequence* OptionalRtxLattice,
        bool                             RebuildVolumeGrids = true);

    [[nodiscard]] bool RenderWorldViewportRgba8(
        WorldRenderTier                Tier,
        const PerspectiveCameraFrame&  Camera,
        const WorldSceneSpecification& Scene,
        uint32_t                       DebugChannel,
        std::vector<uint8_t>&          OutDisplayRgba8);

private:
    void EnsureBufferCapacity(
        DeviceBufferResource& TargetBuffer,
        uint64_t              MinimumBytes,
        VkBufferUsageFlags    UsageMask);

    VulkanInterchange*     BoundDevice              = nullptr;
    ComputeDispatchEncoder DispatchEncoder          {};
    uint32_t               RadiometricProgramHandle = UINT32_MAX;

    DeviceBufferResource   UniformBuffer            {};
    DeviceBufferResource   BvhEntryBuffer           {};
    DeviceBufferResource   ShadedTriangleBuffer     {};
    DeviceBufferResource   MaterialEntryBuffer      {};
    DeviceBufferResource   SignedDistanceBuffer     {};
    DeviceBufferResource   IrradianceLatticeBuffer  {};
    DeviceBufferResource   OutputPixelBuffer        {};

    Mathematics::Axis3D    CachedSdfMinCorner       { -3.5f, -3.5f, -1.2f };
    float                  CachedSdfSpacing         = 0.14f;
    uint32_t               CachedSdfDimX            = 0u;
    uint32_t               CachedSdfDimY            = 0u;
    uint32_t               CachedSdfDimZ            = 0u;

    Mathematics::Axis3D    CachedLatticeMin         { -3.2f, -2.8f, -0.75f };
    Mathematics::Axis3D    CachedLatticeMax         {  3.2f,  3.2f,  2.25f };
    uint32_t               CachedLatticeDimX        = 0u;
    uint32_t               CachedLatticeDimY        = 0u;
    uint32_t               CachedLatticeDimZ        = 0u;

    uint32_t               UploadedBvhCount         = 0u;
    uint32_t               UploadedMaterialCount    = 0u;
    bool                   SceneUploadedOnce        = false;
    float                  LastDispatchMilliseconds = 0.0f;
};

} // namespace Frontier::GeometricRaster
