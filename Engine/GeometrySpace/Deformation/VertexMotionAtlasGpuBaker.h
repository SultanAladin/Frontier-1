/*============================================================================================================================================
                                                  VERTEXMOTIONATLASGPUBAKER.H
============================================================================================================================================*/
// 🧩 Phase G03 Real Async Vulkan 1.2 Compute Baker executing `DeformableVatBake.comp.spv` (`CsVertexMotionAtlasBake`)
//    directly into RGBA32F 2D storage images via VulkanInterchange + isolated ComputeDispatchEncoder +
//    DispatchCommandBufferAsync, with deterministic CPU fallback via VertexMotionAtlasBaker when headless.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"
#include "Engine/SimulationSpace/AsyncGpuReadbackRing.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>

#if __has_include(<volk.h>)
    #define FRONTIER_VATBAKER_HAS_VULKAN 1
    #include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
    #include "Engine/GeometricRaster/VulkanInterchange.h"
#else
    #define FRONTIER_VATBAKER_HAS_VULKAN 0
#endif

namespace Frontier::GeometrySpace::Deformation
{

enum class VatBakeExecutionRoute : uint8_t
{
    HeadlessCpuMirror = 0u, // Deterministic CPU VertexMotionAtlasBaker fallback when VulkanInterchange is absent
    VulkanComputeGpu  = 1u  // Real Async Vulkan 1.2 CsVertexMotionAtlasBake into RGBA32F 2D images (zero mid-frame stalls)
};

struct VatGpuBakerTelemetry
{
    VatBakeExecutionRoute Route                 = VatBakeExecutionRoute::HeadlessCpuMirror;
    uint32_t              BakedSliceCount       = 0u;
    uint32_t              BakedVertexCount      = 0u;
    uint64_t              TotalGpuDispatches    = 0ull;
    uint64_t              LastSubmittedTimeline = 0ull;
    uint64_t              AllocatedImageBytes   = 0ull;
    bool                  RealGpuDispatched     = false;
};

/// Real Async Vulkan 1.2 GPU Vertex Motion Baker with strict zero-stall submission and honest route telemetry.
class VertexMotionAtlasGpuBaker
{
public:
    static constexpr uint32_t kTripleBufferSlots = 3u;

    VertexMotionAtlasGpuBaker() = default;
    ~VertexMotionAtlasGpuBaker();

    VertexMotionAtlasGpuBaker(const VertexMotionAtlasGpuBaker&)            = delete;
    VertexMotionAtlasGpuBaker& operator=(const VertexMotionAtlasGpuBaker&) = delete;

#if FRONTIER_VATBAKER_HAS_VULKAN
    [[nodiscard]] bool AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept;
    [[nodiscard]] const GeometricRaster::DeviceImageResource& InspectVatPosImage() const noexcept { return ImgVatPosOut_; }
    [[nodiscard]] const GeometricRaster::DeviceImageResource& InspectVatNrmImage() const noexcept { return ImgVatNrmOut_; }
#endif

    [[nodiscard]] bool Initialize(const std::filesystem::path& compiledShaderDirectory = "CompiledShaders");
    void Teardown() noexcept;

    /// Bakes `body` + `embeddedSurface` into a `VertexMotionAtlas`. When a live Vulkan 1.2 device & compiled
    /// `DeformableVatBake.comp.spv` are attached, dispatches GPU compute slices asynchronously into RGBA32F 2D images;
    /// otherwise falls back cleanly to deterministic CPU `VertexMotionAtlasBaker::BakeFromDeformable`.
    [[nodiscard]] VertexMotionAtlas Bake(
        SimulationSpace::Deformable::DeformableBody& body,
        const SimulationSpace::Deformable::DeformableSimulationConfiguration& configuration,
        const CageEmbeddedSurface& embeddedSurface,
        const VertexMotionAtlasBakeSettings& settings = {});

    [[nodiscard]] bool Ready() const noexcept { return Initialized_; }
    [[nodiscard]] const VatGpuBakerTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }

private:
    VatGpuBakerTelemetry Telemetry_    {};
    bool                 Initialized_  = false;
    bool                 GpuLiveReady_ = false;

#if FRONTIER_VATBAKER_HAS_VULKAN
    struct PendingCmd
    {
        VkCommandBuffer CmdBuffer      = VK_NULL_HANDLE;
        uint64_t        TimelineSignal = 0ull;
    };

    void RetireCompletedCommandBuffersNonBlocking() noexcept;
    void ReleaseGpuResources() noexcept;

    GeometricRaster::VulkanInterchange*        Interchange_      = nullptr;
    GeometricRaster::ComputeDispatchEncoder    Encoder_          {};
    uint32_t                                   ProgVatBake_      = UINT32_MAX;
    uint32_t                                   ActiveSlot_       = 0u;
    GeometricRaster::DeviceBufferResource      BufVatUbo_        {};
    GeometricRaster::DeviceBufferResource      BufCagePos_       {};
    GeometricRaster::DeviceBufferResource      BufTetCofactors_  {};
    GeometricRaster::DeviceBufferResource      BufBoundVerts_    {};
    GeometricRaster::DeviceImageResource       ImgVatPosOut_     {};
    GeometricRaster::DeviceImageResource       ImgVatNrmOut_     {};
    std::array<PendingCmd, kTripleBufferSlots> InFlightCommands_ {};
#endif
};

} // namespace Frontier::GeometrySpace::Deformation
