/*============================================================================================================================================
                                                    DEFORMABLEVULKANEXECUTOR.H
============================================================================================================================================*/
// 🧩 Phase D14 + Merge-Prep U02: Real Async Vulkan 1.2 Compute SSBO / RGBA32F VAT image executor via VulkanInterchange +
//    ComputeDispatchEncoder (isolated descriptor ring) + 3-slot non-blocking AsyncGpuReadbackRing (zero GPU stalls).
//    CPU mirror executes strictly when (!Interchange || !Interchange->Ready()) for headless fallback/tests,
//    and VulkanComputeGpu is reported ONLY when real GPU buffers, SPIR-V pipelines, and vkCmdDispatch ran.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"
#include "Engine/SimulationSpace/AsyncGpuReadbackRing.h"
#include "Engine/SimulationSpace/Deformable/DeformableGpuDispatch.h"
#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"

#if __has_include(<volk.h>)
#define FRONTIER_DEFORMABLE_HAS_VULKAN 1
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#else
#define FRONTIER_DEFORMABLE_HAS_VULKAN 0
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 EXECUTOR TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

enum class DeformableExecutionRoute : uint8_t
{
    HeadlessCpuMirror = 0u, // Headless CPU fallback (used ONLY when VulkanInterchange is absent/unready)
    VulkanComputeGpu  = 1u  // Real Async Vulkan 1.2 vkCmdDispatch compute execution (zero mid-frame stalls)
};

struct DeformableExecutorTelemetry
{
    DeformableExecutionRoute Route                  = DeformableExecutionRoute::HeadlessCpuMirror;
    uint32_t                 ActiveRingSlot         = 0u;
    uint64_t                 CompletedTicks         = 0ull;
    uint64_t                 TotalDispatches        = 0ull;
    uint64_t                 TotalMemoryBarriers    = 0ull;
    uint64_t                 AllocatedSsboBytes     = 0ull;
    uint64_t                 AllocatedVatImageBytes = 0ull;
    uint64_t                 LastSubmittedTimeline  = 0ull;
    uint32_t                 LastSubstepCount       = 0u;
    uint32_t                 LastDispatchCount      = 0u;
    bool                     RealGpuDispatched      = false;
};

/// Read-only GPU buffer/image handle snapshot consumed by SimulationGpuRenderFeed with zero CPU readback.
struct DeformableGpuRenderFeedHandle
{
    DeformableExecutionRoute Route                 = DeformableExecutionRoute::HeadlessCpuMirror;
    uint32_t                 PointCount            = 0u;
    uint32_t                 TetCount              = 0u;
    uint64_t                 LastSubmittedTimeline = 0ull;
    uint64_t                 PositionBufferHandle  = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    uint64_t                 CofactorBufferHandle  = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    uint64_t                 VatPosViewHandle      = 0ull; // VkImageView cast to uint64_t (0 when headless)
    uint64_t                 VatNrmViewHandle      = 0ull; // VkImageView cast to uint64_t (0 when headless)
    bool                     HasVatTexture         = false;
    bool                     GpuResident           = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE VULKAN EXECUTOR
//------------------------------------------------------------------------------------------------------------------------

class DeformableVulkanExecutor
{
public:
    static constexpr uint32_t kTripleBufferSlots = 3u;
    static constexpr uint32_t kMaxRigidProxies   = 64u;

    DeformableVulkanExecutor() = default;
    ~DeformableVulkanExecutor();

    DeformableVulkanExecutor(const DeformableVulkanExecutor&)            = delete;
    DeformableVulkanExecutor& operator=(const DeformableVulkanExecutor&) = delete;

    /// Initializes triple-buffered GPU SSBOs + ComputeDispatchEncoder (when VulkanInterchange is ready & SPIR-V
    /// directory resolves) or headless CPU mirror staging rings (zero per-tick heap allocations thereafter).
    [[nodiscard]] bool InitializeForBody(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders");

    /// Uploads a baked RGBA32F Vertex Motion Table into real GPU 2D images (when GPU-backed) or tracks bytes headlessly.
    [[nodiscard]] bool UploadVertexMotionAtlas(
        const GeometrySpace::Deformation::VertexMotionAtlas& motionTable);

    /// Zero-allocation tick: updates pre-allocated ReusablePlan_ and executes asynchronously on GPU (real vkCmdDispatch +
    /// DispatchCommandBufferAsync + non-blocking QueryTimelineReached readback) or CPU fallback when headless.
    const DeformableSubstepDispatchPlan& ExecuteTickInPlace(
        DeformableBody& body,
        float dtSeconds,
        const DeformableSimulationConfiguration& configuration,
        float simTimeSeconds,
        float windPulse = 0.0f,
        const DeformableExternalCoupling* externalCoupling = nullptr,
        bool appendVatBakeStep = false,
        uint32_t highPolyVertexCount = 0u);

    /// Backward-compatible value-returning wrapper delegating to ExecuteTickInPlace.
    [[nodiscard]] DeformableSubstepDispatchPlan ExecuteTick(
        DeformableBody& body,
        float dtSeconds,
        const DeformableSimulationConfiguration& configuration,
        float simTimeSeconds,
        float windPulse = 0.0f,
        const DeformableExternalCoupling* externalCoupling = nullptr,
        bool appendVatBakeStep = false,
        uint32_t highPolyVertexCount = 0u);

    /// Non-blocking poll of the 3-slot GPU->CPU impulse readback ring (1-2 frames latency, zero stall).
    [[nodiscard]] bool PollAsyncImpulseReadback() noexcept;
    [[nodiscard]] std::span<const PhysicsSpace::RigidImpulseAccumulatorEntry> InspectCompletedImpulseFeedback() const noexcept
    {
        return ImpulseReadbackRing_.InspectLatestCompleted();
    }

    void Teardown() noexcept;

    [[nodiscard]] bool Ready() const noexcept { return Initialized_; }
    [[nodiscard]] const DeformableExecutorTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }
    [[nodiscard]] DeformableGpuRenderFeedHandle      InspectRenderFeedHandle() const noexcept;
    [[nodiscard]] const DeformableGpuBufferPack& InspectActiveGpuPack() const noexcept
    {
        return RingPacks_[Telemetry_.ActiveRingSlot];
    }
    [[nodiscard]] const DeformableSubstepDispatchPlan& InspectLastDispatchPlan() const noexcept
    {
        return ReusablePlan_;
    }

#if FRONTIER_DEFORMABLE_HAS_VULKAN
    /// Attaches a live VulkanInterchange. Note: Telemetry_.Route stays HeadlessCpuMirror until InitializeForBody()
    /// successfully allocates real GPU SSBOs and registers compiled .slang SPIR-V compute pipelines.
    [[nodiscard]] bool AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept;
    [[nodiscard]] const GeometricRaster::DeviceBufferResource& InspectPositionsBuffer() const noexcept
    {
        return BufPositionsInvMass_;
    }
    [[nodiscard]] const GeometricRaster::DeviceBufferResource& InspectVelocitiesBuffer() const noexcept
    {
        return BufVelocitiesFlags_;
    }
#endif

private:
    std::array<DeformableGpuBufferPack, kTripleBufferSlots> RingPacks_    {};
    DeformableSubstepDispatchPlan                           ReusablePlan_ {};
    DeformableStepUniforms                                  LastUniforms_ {};
    DeformableExecutorTelemetry                             Telemetry_    {};
    AsyncGpuReadbackRing<PhysicsSpace::RigidImpulseAccumulatorEntry, kTripleBufferSlots> ImpulseReadbackRing_ {};
    std::vector<PhysicsSpace::RigidImpulseAccumulatorEntry> ScratchImpulses_ {};
    bool                                                    Initialized_  = false;
    bool                                                    HasVatImage_  = false;
    bool                                                    GpuLiveReady_ = false;

#if FRONTIER_DEFORMABLE_HAS_VULKAN
    struct PendingCommandSlot
    {
        VkCommandBuffer CmdBuffer      = VK_NULL_HANDLE;
        uint64_t        TimelineSignal = 0ull;
    };

    void RetireCompletedCommandBuffersNonBlocking() noexcept;
    [[nodiscard]] bool InitializeGpuResources(
        const DeformableGpuBufferPack& seedPack,
        const std::filesystem::path& shaderDir);
    void ReleaseGpuResources() noexcept;
    void DispatchGpuAsync(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        float subDt,
        float simTimeSeconds,
        const DeformableExternalCoupling* externalCoupling) noexcept;

    GeometricRaster::VulkanInterchange*                                   Interchange_          = nullptr;
    GeometricRaster::ComputeDispatchEncoder                               Encoder_              {};
    uint32_t                                                              ProgPredict_          = UINT32_MAX;
    uint32_t                                                              ProgDistance_         = UINT32_MAX;
    uint32_t                                                              ProgBend_             = UINT32_MAX;
    uint32_t                                                              ProgNeoHookean_       = UINT32_MAX;
    uint32_t                                                              ProgContact_          = UINT32_MAX;
    uint32_t                                                              ProgVatBake_          = UINT32_MAX;
    static constexpr uint32_t kStepUboStrideBytes = 256u;
    static constexpr uint32_t kMaxStepUboSlices   = 256u;
    std::array<GeometricRaster::DeviceBufferResource, kTripleBufferSlots> UniformBuffers_       {};
    std::array<PendingCommandSlot, kTripleBufferSlots>                    InFlightCommands_     {};
    PendingCommandSlot                                                    InitCommand_          {};
    GeometricRaster::DeviceBufferResource                                 BufPositionsInvMass_  {};
    GeometricRaster::DeviceBufferResource                                 BufPrevPosStrain_     {};
    GeometricRaster::DeviceBufferResource                                 BufVelocitiesFlags_   {};
    GeometricRaster::DeviceBufferResource                                 BufRimLocalOffsets_   {};
    GeometricRaster::DeviceBufferResource                                 BufDistanceCons_      {};
    GeometricRaster::DeviceBufferResource                                 BufDistanceOrder_     {};
    GeometricRaster::DeviceBufferResource                                 BufBendCons_          {};
    GeometricRaster::DeviceBufferResource                                 BufBendOrder_         {};
    GeometricRaster::DeviceBufferResource                                 BufTetElements_       {};
    GeometricRaster::DeviceBufferResource                                 BufTetOrder_          {};
    GeometricRaster::DeviceBufferResource                                 BufTetCofactors_      {};
    GeometricRaster::DeviceBufferResource                                 BufRigidProxies_      {};
    GeometricRaster::DeviceBufferResource                                 BufImpulseFeedback_   {};
    GeometricRaster::DeviceImageResource                                  VatPosImage_          {};
    GeometricRaster::DeviceImageResource                                  VatNrmImage_          {};
#endif
};

} // namespace Frontier::SimulationSpace::Deformable
