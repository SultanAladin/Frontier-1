/*============================================================================================================================================
                                                       PARTICLEGPUSIMULATION.H
============================================================================================================================================*/
// 🧩 Core ParticleSpace GPU uniform layout, anchor table builder, per-cloud state manager & bit-accurate CPU mirror of ParticleUpdateCompute.slang
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L9-L752 & ParticleSimulation.js L1-L340

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/CarrierSolidRegistry.h"
#include "Engine/ParticleSpace/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/SimulationSpace/AsyncGpuReadbackRing.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#if __has_include(<volk.h>)
#define FRONTIER_PARTICLE_HAS_VULKAN 1
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#else
#define FRONTIER_PARTICLE_HAS_VULKAN 0
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 GPU ABI DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Matches ParticleStateRecord in ParticleUpdateCompute.slang byte-for-byte (32 bytes, 16-byte aligned).
struct alignas(16) ParticleStateGpuRecord
{
    std::array<float, 4> PositionAge  { 0.0f, -1.0e4f, 0.0f, 1.0f }; // xyz = position (m), w = age (s)
    std::array<float, 4> VelocitySeed { 0.0f, 0.0f, 0.0f, 0.0f };    // xyz = velocity (m/s), w = seed [0..1]
};
static_assert(sizeof(ParticleStateGpuRecord) == 32u, "ParticleStateGpuRecord must be 32 bytes for Slang SSBO parity");

/// Matches ParticleShapeUniformBlock in ParticleShapeAnchor.slang byte-for-byte (176 bytes, 16-byte aligned).
struct alignas(16) ParticleShapeUniformBlock
{
    std::array<float, 4> OriginRadius        { 0.0f, 0.0f, 0.0f, 0.5f };
    std::array<float, 4> AxisFill            { 0.0f, 1.0f, 0.0f, 0.0f };
    std::array<float, 4> ShapeCounts         { 0.0f, 2048.0f, 8.0f, 0.0f };
    std::array<float, 4> AnchorLayout        { 1.0f, 0.0f, 48.0f, 0.0f };
    std::array<float, 4> MocapParams         { 0.0f, 0.0f, 1.0f, 1.0f };
    std::array<float, 4> MocapKickEffector   { 0.0f, 0.0f, 0.0f, 1.8f };
    std::array<float, 4> EffectorMotion      { 1.6f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> EffectorAxisDerez   { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> DerezFrontParams    { 0.0f, 0.14f, 0.0f, 0.0f };
    std::array<float, 4> DerezEdgeColourWing { 0.0f, 0.9411765f, 1.0f, 10.0f };
    std::array<float, 4> GlitchParams        { 0.0f, 14.0f, 0.0f, 0.0f };
};
static_assert(sizeof(ParticleShapeUniformBlock) == 176u, "ParticleShapeUniformBlock must be 176 bytes");

/// Matches ParticleUpdateUniformBlock in ParticleUpdateCompute.slang byte-for-byte (304 bytes, 16-byte aligned).
struct alignas(16) ParticleUpdateUniformBlock
{
    ParticleShapeUniformBlock Shape {};
    std::array<float, 4>      GridCentreHalfX   { 0.0f, 2.0f, 0.0f, 2.5f };
    std::array<float, 4>      GridHalfYZTimeDt  { 2.0f, 2.5f, 0.0f, 0.01666667f };
    std::array<float, 4>      SpawnKinematics   { 1.0f, 8.0f, 3.0f, 2.0f };
    std::array<float, 4>      LaunchForces      { 0.5f, 0.0f, 0.0f, 0.5f };
    std::array<float, 4>      CouplingNoise     { 0.0f, 1.0f, 0.0f, 1.3f };
    std::array<float, 4>      SpringBounce      { 6.0f, 3.0f, 0.2f, 0.5f };
    std::array<float, 4>      DissolveSwarm     { 0.0f, 0.6f, 0.0f, 0.0f };
    std::array<float, 4>      SwarmControlFlags { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert(sizeof(ParticleUpdateUniformBlock) == 304u, "ParticleUpdateUniformBlock must be 304 bytes");

/// Matches ParticleTrailUpdateUniformBlock in ParticleTrailUpdateCompute.slang byte-for-byte (32 bytes, 16-byte aligned).
struct alignas(16) ParticleTrailUpdateUniformBlock
{
    std::array<float, 4> LayoutHeadAdvance { 2304.0f, 24.0f, 0.0f, 0.0f }; // capacity, trailLength, head, advance
    std::array<float, 4> ResetLifetimePad  { 1.0f, 3.0f, 0.0f, 0.0f };     // reset, lifetime (0 for Cloner), pad, pad
};
static_assert(sizeof(ParticleTrailUpdateUniformBlock) == 32u, "ParticleTrailUpdateUniformBlock must be 32 bytes");

struct DerezEvaluationReading
{
    float WaveU    = 0.5f;
    float Progress = 0.0f;
    float EdgeBand = 0.0f;
    bool  Released = false;
};

struct ParticleSimulationRegion
{
    std::array<float, 3> CentreMeters     { 0.0f, 2.0f, 0.0f };
    std::array<float, 3> HalfExtentMeters { 2.5f, 2.0f, 2.5f };
};

struct ParticleSimulationStatistics
{
    uint32_t ActiveCloudCount     = 0u;
    uint32_t ActiveParticleCount  = 0u;
    uint32_t DispatchWorkgroups   = 0u;
};

using GasVelocityQueryFn = std::function<std::array<float, 3>(const std::array<float, 3>& worldPos)>;

/// Zero-allocation function-pointer + opaque context view for sampling fluid velocity on hot loops (0 heap allocs/tick).
struct GasVelocitySamplerView
{
    using SampleCallbackFn = std::array<float, 3>(*)(const void* userContext, const std::array<float, 3>& worldPos) noexcept;
    SampleCallbackFn Callback    = nullptr;
    const void*      UserContext = nullptr;

    [[nodiscard]] explicit operator bool() const noexcept { return Callback != nullptr; }
    [[nodiscard]] std::array<float, 3> Sample(const std::array<float, 3>& worldPos) const noexcept
    {
        return (Callback != nullptr) ? Callback(UserContext, worldPos) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
    }
};

/// Per-cloud ping-pong particle state, previous positions, and baked anchor texels.
struct ParticleCloudStateRecord
{
    std::string                         NodeId             {};
    uint32_t                            TextureSide        = 48u;
    uint32_t                            Capacity           = 2304u;
    uint32_t                            ActiveCount        = 2048u;
    bool                                NeedsReset         = true;
    uint64_t                            FormationHash      = 0ull;
    uint64_t                            AnchorHash         = 0ull;
    std::string                         FormationSignature {};
    std::string                         AnchorSignature    {};
    AnchorTexelTable                    AnchorTexels       {};
    std::vector<ParticleStateGpuRecord> ReadStates         {};
    std::vector<ParticleStateGpuRecord> WriteStates        {};
    std::vector<std::array<float, 4>>   PreviousPositions  {};
    ParticleUpdateUniformBlock          LastUniformBlock   {};

    // On-demand spline ribbon history ring buffer (ParticleSimulation.js L82-L95, L147-L172)
    uint32_t                            TrailLength        = 0u;
    uint32_t                            TrailHead          = 0u;
    float                               TrailClock         = 0.0f;
    bool                                TrailNeedsReset    = true;
    std::vector<std::array<float, 4>>   TrailSamples       {};
    std::vector<std::array<float, 4>>   TrailScratch       {};
    ParticleTrailUpdateUniformBlock     LastTrailUniforms  {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           STATELESS CPU SHADER MIRROR
//------------------------------------------------------------------------------------------------------------------------

/// Populates ParticleShapeUniformBlock from evaluated cloud parameters & anchor layout.
[[nodiscard]] ParticleShapeUniformBlock BuildParticleShapeUniforms(
    const ParticleCloudParameters& params,
    uint32_t textureSide,
    uint32_t anchorWidth,
    uint32_t anchorCount) noexcept;

/// Populates ParticleUpdateUniformBlock for compute dispatch or CPU stepping.
[[nodiscard]] ParticleUpdateUniformBlock BuildParticleUpdateUniforms(
    const ParticleCloudNode& node,
    uint32_t textureSide,
    uint32_t anchorWidth,
    uint32_t anchorCount,
    const ParticleSimulationRegion& region,
    float simulationTimeSeconds,
    float deltaTimeSeconds,
    bool resetFlag,
    uint32_t forceRecordCount,
    bool hasGasVolume) noexcept;

/// Bit-accurate CPU mirror of ParticleShapeAnchor.slang::ShapePointAtTime.
[[nodiscard]] std::array<float, 3> EvaluateShapePointAtTime(
    const ParticleShapeUniformBlock& shape,
    std::span<const float> anchorTexelRgba,
    int32_t index,
    const std::array<float, 3>& rnd,
    bool lattice,
    float timeSeconds,
    std::array<float, 3>* outShapeVelocity = nullptr) noexcept;

/// Bit-accurate CPU mirror of ParticleShapeAnchor.slang::EvaluateEffector.
[[nodiscard]] float EvaluateEffectorWeight(
    const ParticleShapeUniformBlock& shape,
    const std::array<float, 3>& worldPosition,
    float timeSeconds) noexcept;

/// Bit-accurate CPU mirror of ParticleShapeAnchor.slang::EvaluateDerez.
[[nodiscard]] DerezEvaluationReading EvaluateDerezFront(
    const ParticleShapeUniformBlock& shape,
    const std::array<float, 3>& restHome,
    float timeSeconds) noexcept;

/// Zero-allocation CPU mirror of ParticleUpdateCompute.slang::ComputeEntry using GasVelocitySamplerView.
void StepParticleCloudCpu(
    ParticleCloudStateRecord& record,
    const ParticleUpdateUniformBlock& uniforms,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
    GasVelocitySamplerView gasVelocitySampler);

/// Backward-compatible overload accepting std::function GasVelocityQueryFn.
void StepParticleCloudCpu(
    ParticleCloudStateRecord& record,
    const ParticleUpdateUniformBlock& uniforms,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords = {},
    const GasVelocityQueryFn& gasVelocityQuery = {});

/// Bit-accurate CPU mirror of ParticleTrailUpdateCompute.slang::ComputeEntry (ring-buffer history up to 96 samples).
void StepTrailHistoryCpu(
    ParticleCloudStateRecord& record,
    const ParticleTrailUpdateUniformBlock& uniforms);

//------------------------------------------------------------------------------------------------------------------------
//                                                PARTICLEGPUSIMULATION
//------------------------------------------------------------------------------------------------------------------------

/// Manages per-cloud GPU/CPU state records, anchor texture refreshes, and simulation stepping.
class ParticleGpuSimulation
{
public:
    ParticleGpuSimulation() noexcept = default;
    ~ParticleGpuSimulation() noexcept = default;

    void ClearAll() noexcept;
    void ResetAll() noexcept;
    void ResetCloud(std::string_view nodeId) noexcept;
    void PruneDeadClouds(std::span<const ParticleCloudNode> liveNodes);

    [[nodiscard]] ParticleCloudStateRecord& EnsureCloudRecord(
        const ParticleCloudNode& node,
        const CarrierSolidRegistry& solidRegistry,
        float simulationTimeSeconds);

    [[nodiscard]] const ParticleCloudStateRecord* FindCloudRecord(std::string_view nodeId) const noexcept;
    [[nodiscard]] ParticleSimulationStatistics QueryStatistics() const noexcept { return Statistics; }

    /// Zero-allocation CPU stepping overload using GasVelocitySamplerView (0 heap allocations in steady state).
    void StepCloudsCpu(
        std::span<const ParticleCloudNode> clouds,
        const CarrierSolidRegistry& solidRegistry,
        const ParticleSimulationRegion& region,
        float simulationTimeSeconds,
        float deltaTimeSeconds,
        std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
        GasVelocitySamplerView gasVelocitySampler);

    /// Backward-compatible overload accepting std::function GasVelocityQueryFn.
    void StepCloudsCpu(
        std::span<const ParticleCloudNode> clouds,
        const CarrierSolidRegistry& solidRegistry,
        const ParticleSimulationRegion& region,
        float simulationTimeSeconds,
        float deltaTimeSeconds,
        std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords = {},
        const GasVelocityQueryFn& gasVelocityQuery = {});

private:
    void RefreshCloudAnchors(
        ParticleCloudStateRecord& record,
        const ParticleCloudNode& node,
        const CarrierSolidRegistry& solidRegistry,
        float simulationTimeSeconds);

    std::vector<ParticleCloudStateRecord> Records    {};
    ParticleSimulationStatistics          Statistics {};
};

//------------------------------------------------------------------------------------------------------------------------
//                             REAL ASYNC VULKAN 1.2 PARTICLE GPU EXECUTOR (MERGE-PREP U03)
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleExecutionRoute : uint8_t
{
    HeadlessCpuMirror = 0u, // Headless CPU fallback ONLY when VulkanInterchange is unavailable/unready
    VulkanComputeGpu  = 1u  // Real Async Vulkan 1.2 SSBO vkCmdDispatch execution (zero mid-frame stalls)
};

struct ParticleGpuExecutorTelemetry
{
    ParticleExecutionRoute Route                     = ParticleExecutionRoute::HeadlessCpuMirror;
    uint32_t               ActiveRingSlot            = 0u;
    uint32_t               ActiveParticleCount       = 0u;
    uint32_t               ActiveRigidColliderCount  = 0u;
    uint64_t               CompletedTicks            = 0ull;
    uint64_t               TotalDispatches           = 0ull;
    uint64_t               AdditivePhysicsDispatches = 0ull;
    uint64_t               LastSubmittedTimeline     = 0ull;
    uint64_t               AllocatedSsboBytes        = 0ull;
    bool                   RealGpuDispatched         = false;
    bool                   CollisionPipelineReady    = false;
    bool                   SpatialHashPipelineReady  = false;
    bool                   XpbdPipelineReady         = false;
    bool                   ExternalGasVolumeBound    = false;
};

/// Optional additive GPU physics switches for Layer 220 (XPBD) & Layer 221 (Jolt + Spatial-Hash collisions).
struct ParticleAdditiveGpuPhysicsConfig
{
    bool  EnableRigidCollisions          = false;
    bool  EnableSpatialHashSelfCollision = false;
    bool  EnableXpbdConstraints          = false;
    float Restitution                    = 0.35f;
    float Friction                       = 0.45f;
    float ParticleRadiusMeters           = 0.04f;
    uint32_t XpbdSubsteps                = 4u;
};

/// Packed host upload descriptor for Layer 220 GPU Orthotropic XPBD constraints (matches ParticleXpbdCompute.slang).
struct ParticleXpbdGpuNetworkUpload
{
    uint32_t                  PointCount          = 0u;
    uint32_t                  FaceCount           = 0u;
    uint32_t                  DistanceCount       = 0u;
    uint32_t                  SeamCount           = 0u;
    uint32_t                  HingeCount          = 0u;
    uint32_t                  DualFloatCount      = 0u;
    std::span<const float>    PositionsInvMass4   {}; // PointCount * 4 floats
    std::span<const uint32_t> TopologyWords       {}; // Packed U32Topology
    std::span<const float>    RestTables          {}; // Packed F32Rest
};

/// Read-only GPU buffer feed snapshot consumed by SimulationGpuRenderFeed with zero CPU readback.
struct ParticleGpuRenderFeedHandle
{
    ParticleExecutionRoute Route                 = ParticleExecutionRoute::HeadlessCpuMirror;
    uint32_t               ActiveParticleCount   = 0u;
    uint32_t               MaxParticleCapacity   = 0u;
    uint64_t               LastSubmittedTimeline = 0ull;
    uint64_t               StateBufferHandle     = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    uint64_t               PrevPosBufferHandle   = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    uint64_t               AnchorBufferHandle    = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    uint64_t               ImpulseBufferHandle   = 0ull; // VkBuffer cast to uint64_t (0 when headless)
    bool                   GpuResident           = false;
};

/// 64-byte ABI-identical mirror of RigidImpulseAccumulatorEntry for async Particle->Rigid impulse readback.
struct alignas(16) ParticleRigidImpulseEntry
{
    uint32_t BodySlotIndex      = 0u;
    uint32_t ContactSampleCount = 0u;
    uint32_t Reserved[2]        = { 0u, 0u };
    float    LinearImpulseNs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float    AngularImpulse[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
    float    ContactCentroid[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert(sizeof(ParticleRigidImpulseEntry) == 64u, "ParticleRigidImpulseEntry must remain 64 bytes.");

/// Real Async Vulkan 1.2 Particle compute executor using VulkanInterchange + isolated ComputeDispatchEncoder +
/// DispatchCommandBufferAsync + 3-slot non-blocking AsyncGpuReadbackRing for Particle->Rigid atomic impulse feedback.
class ParticleVulkanGpuExecutor
{
public:
    static constexpr uint32_t kTripleBufferSlots    = 3u;
    static constexpr uint32_t kMaxImpulseEntries    = 64u;
    static constexpr uint32_t kMaxRigidProxies      = 64u;
    static constexpr uint32_t kSpatialHashBuckets   = 4096u;

    ParticleVulkanGpuExecutor() = default;
    ~ParticleVulkanGpuExecutor();

    ParticleVulkanGpuExecutor(const ParticleVulkanGpuExecutor&)            = delete;
    ParticleVulkanGpuExecutor& operator=(const ParticleVulkanGpuExecutor&) = delete;

#if FRONTIER_PARTICLE_HAS_VULKAN
    [[nodiscard]] bool AttachVulkanInterchange(GeometricRaster::VulkanInterchange* interchange) noexcept;
#endif

    [[nodiscard]] bool Initialize(
        uint32_t maxParticleCapacity = 4096u,
        const std::filesystem::path& compiledShaderDirectory = "CompiledShaders");

    void Teardown() noexcept;

    void ConfigureAdditiveGpuPhysics(const ParticleAdditiveGpuPhysicsConfig& config) noexcept
    {
        AdditiveConfig_ = config;
    }
    [[nodiscard]] const ParticleAdditiveGpuPhysicsConfig& InspectAdditiveGpuPhysics() const noexcept
    {
        return AdditiveConfig_;
    }

    /// Uploads a conflict-colored Layer 220 GPU XPBD network into device SSBOs (or tracks counts headlessly).
    [[nodiscard]] bool UploadXpbdNetwork(const ParticleXpbdGpuNetworkUpload& networkUpload);
    [[nodiscard]] uint32_t QueryUploadedXpbdPointCount() const noexcept { return XpbdPointCount_; }

    /// Binds a live GPU Fluid 3D velocity VkImageView (as uint64_t) for zero-copy GPU Fluid->Particle advection.
    /// Passing 0 or `gpuResident == false` falls back to the executor's internal 4x4x4 fallback gas volume.
    void BindExternalFluidVelocityView(uint64_t velocityImageViewHandle, bool gpuResident) noexcept
    {
        ExternalFluidVelocityView_        = (gpuResident && velocityImageViewHandle != 0ull) ? velocityImageViewHandle : 0ull;
        Telemetry_.ExternalGasVolumeBound = (ExternalFluidVelocityView_ != 0ull);
    }
    [[nodiscard]] uint64_t QueryBoundFluidVelocityView() const noexcept { return ExternalFluidVelocityView_; }

    /// Executes one particle tick: dispatches real Vulkan 1.2 compute shaders asynchronously when GPU-backed
    /// (including Layer 221 rigid/spatial-hash collisions & Layer 220 XPBD when enabled), or headless fallback.
    void ExecuteStep(
        ParticleGpuSimulation& headlessFallbackSim,
        std::span<const ParticleCloudNode> clouds,
        const CarrierSolidRegistry& solidRegistry,
        const ParticleSimulationRegion& region,
        float simulationTimeSeconds,
        float deltaTimeSeconds,
        std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
        GasVelocitySamplerView headlessGasSampler = {},
        const void* rigidProxyBytes = nullptr,
        uint32_t rigidProxyCount = 0u,
        uint32_t rigidProxyStrideBytes = 80u);

    /// Non-blocking poll of the 3-slot Particle->Rigid impulse readback ring (1-2 frames latency, zero stall).
    [[nodiscard]] bool PollAsyncImpulseReadback() noexcept;
    [[nodiscard]] std::span<const ParticleRigidImpulseEntry> InspectCompletedImpulseFeedback() const noexcept
    {
        return ImpulseReadbackRing_.InspectLatestCompleted();
    }

    [[nodiscard]] ParticleGpuRenderFeedHandle InspectRenderFeedHandle() const noexcept;

    [[nodiscard]] bool Ready() const noexcept { return Initialized_; }
    [[nodiscard]] const ParticleGpuExecutorTelemetry& InspectTelemetry() const noexcept { return Telemetry_; }

#if FRONTIER_PARTICLE_HAS_VULKAN
    [[nodiscard]] const GeometricRaster::DeviceBufferResource& InspectActiveStateBuffer() const noexcept
    {
        return BufStates_[ActivePong_];
    }
#endif

private:
    uint32_t                         MaxParticleCapacity_       = 4096u;
    uint32_t                         ActivePong_                = 0u;
    uint64_t                         ExternalFluidVelocityView_ = 0ull;
    uint32_t                         XpbdPointCount_            = 0u;
    uint32_t                         XpbdFaceCount_             = 0u;
    uint32_t                         XpbdDistanceCount_         = 0u;
    uint32_t                         XpbdSeamCount_             = 0u;
    uint32_t                         XpbdHingeCount_            = 0u;
    uint32_t                         XpbdDualFloatCount_        = 0u;
    ParticleAdditiveGpuPhysicsConfig AdditiveConfig_            {};
    ParticleGpuExecutorTelemetry     Telemetry_           {};
    SimulationSpace::AsyncGpuReadbackRing<ParticleRigidImpulseEntry, kTripleBufferSlots> ImpulseReadbackRing_ {};
    bool                             Initialized_         = false;
    bool                             GpuLiveReady_        = false;

#if FRONTIER_PARTICLE_HAS_VULKAN
    struct PendingCmd
    {
        VkCommandBuffer CmdBuffer      = VK_NULL_HANDLE;
        uint64_t        TimelineSignal = 0ull;
    };

    void RetireCompletedCommandBuffersNonBlocking() noexcept;
    [[nodiscard]] bool InitializeGpuResources(const std::filesystem::path& shaderDir);
    void ReleaseGpuResources() noexcept;
    void DispatchGpuAsync(
        std::span<const ParticleCloudNode> clouds,
        const ParticleSimulationRegion& region,
        float simulationTimeSeconds,
        float deltaTimeSeconds,
        std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords,
        const void* rigidProxyBytes,
        uint32_t rigidProxyCount,
        uint32_t rigidProxyStrideBytes) noexcept;

    GeometricRaster::VulkanInterchange*                   Interchange_       = nullptr;
    GeometricRaster::ComputeDispatchEncoder               Encoder_           {};
    VkSampler                                             LinearSampler_     = VK_NULL_HANDLE;
    uint32_t                                              ProgUpdate_        = UINT32_MAX;
    uint32_t                                              ProgTrail_         = UINT32_MAX;
    uint32_t                                              ProgCollision_     = UINT32_MAX;
    uint32_t                                              ProgHashClear_     = UINT32_MAX;
    uint32_t                                              ProgHashCount_     = UINT32_MAX;
    uint32_t                                              ProgHashScanLocal_ = UINT32_MAX;
    uint32_t                                              ProgHashScanBlocks_= UINT32_MAX;
    uint32_t                                              ProgHashScanAdd_   = UINT32_MAX;
    uint32_t                                              ProgHashScatter_   = UINT32_MAX;
    uint32_t                                              ProgHashResolve_   = UINT32_MAX;
    uint32_t                                              ProgXpbdPredict_   = UINT32_MAX;
    uint32_t                                              ProgXpbdResetDual_ = UINT32_MAX;
    uint32_t                                              ProgXpbdDistance_  = UINT32_MAX;
    uint32_t                                              ProgXpbdSeam_      = UINT32_MAX;
    uint32_t                                              ProgXpbdFace_      = UINT32_MAX;
    uint32_t                                              ProgXpbdHinge_     = UINT32_MAX;
    uint32_t                                              ProgXpbdFinalize_  = UINT32_MAX;
    std::array<GeometricRaster::DeviceBufferResource, 2>  BufStates_         {};
    GeometricRaster::DeviceBufferResource                 BufPrevPos_        {};
    GeometricRaster::DeviceBufferResource                 BufAnchors_        {};
    GeometricRaster::DeviceBufferResource                 BufForces_         {};
    GeometricRaster::DeviceBufferResource                 BufRigidProxies_   {};
    GeometricRaster::DeviceBufferResource                 BufProxyOmega_     {};
    GeometricRaster::DeviceBufferResource                 BufHullHeaders_    {};
    GeometricRaster::DeviceBufferResource                 BufHullVoxels_     {};
    GeometricRaster::DeviceBufferResource                 BufImpulseAtomic_  {};
    GeometricRaster::DeviceBufferResource                 BufHashCounts_     {};
    GeometricRaster::DeviceBufferResource                 BufHashOffsets_    {};
    GeometricRaster::DeviceBufferResource                 BufHashBlockSums_  {};
    GeometricRaster::DeviceBufferResource                 BufHashWriteOffs_  {};
    GeometricRaster::DeviceBufferResource                 BufHashBuckets_    {};
    GeometricRaster::DeviceBufferResource                 BufHashSortedIdx_  {};
    GeometricRaster::DeviceBufferResource                 BufXpbdPosInvMass_ {};
    GeometricRaster::DeviceBufferResource                 BufXpbdPrevPos_    {};
    GeometricRaster::DeviceBufferResource                 BufXpbdVelocities_ {};
    GeometricRaster::DeviceBufferResource                 BufXpbdTopology_   {};
    GeometricRaster::DeviceBufferResource                 BufXpbdRestTables_ {};
    GeometricRaster::DeviceBufferResource                 BufXpbdDuals_      {};
    GeometricRaster::DeviceImageResource                  ImgFallbackGas_    {};
    std::array<GeometricRaster::DeviceBufferResource, kTripleBufferSlots> BufUpdateUbo_ {};
    std::array<GeometricRaster::DeviceBufferResource, kTripleBufferSlots> BufCollisionUbo_ {};
    std::array<GeometricRaster::DeviceBufferResource, kTripleBufferSlots> BufHashUbo_ {};
    std::array<GeometricRaster::DeviceBufferResource, kTripleBufferSlots> BufXpbdUbo_ {};
    std::array<PendingCmd, kTripleBufferSlots>            InFlightCommands_  {};
    PendingCmd                                            InitCommand_       {};
#endif
};

} // namespace Frontier::ParticleSpace
