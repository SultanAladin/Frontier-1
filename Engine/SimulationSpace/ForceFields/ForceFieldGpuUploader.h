/*============================================================================================================================================
                                                          FORCEFIELDGPUUPLOADER.H
============================================================================================================================================*/
// 🧩 100% GPU gizmo line-list builder (all 20 force models) and triple-buffered Vulkan SSBO / vertex uploader
// Reference: Experimental/FluidEditor/Engine/VolumeRenderer.js L575-L994, FluidSolver.js L419-L438

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#if __has_include(<volk.h>)
#define FRONTIER_FORCEFIELD_HAS_VULKAN 1
#include "Engine/GeometricRaster/VulkanInterchange.h"
#else
#define FRONTIER_FORCEFIELD_HAS_VULKAN 0
#endif

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// 32-byte GPU line-list vertex consumed by ForceFieldOverlayRasterization.slang (CLAUDE.md §2.2 100% GPU gizmos).
struct alignas(16) ForceGizmoVertex
{
    float Position[3] = { 0.0f, 0.0f, 0.0f }; // [m] - World-space line endpoint
    float Padding     = 0.0f;                 // [-] - 16-byte alignment padding
    float Colour[4]   = { 1.0f, 1.0f, 1.0f, 1.0f }; // [-] - RGBA line tint
};

static_assert(sizeof(ForceGizmoVertex) == 32u, "ForceGizmoVertex must remain 32 bytes for GPU vertex input stride.");

/// Push-constant block shared by ForceFieldOverlayRasterization.slang gizmo & force-vector passes.
struct alignas(16) ForceOverlayPushConstants
{
    float    ViewProjection[16] = {}; // [-] - Row-major 4x4 ViewProjection matrix
    float    GridOriginCell[4]  = {}; // [m] - xyz = region origin, w = cellSize
    uint32_t GridExtentMode[4]  = {}; // [-] - xyz = grid dims, w = mode (0 slice, 1 sparse)
    uint32_t AxisSliceStride[4] = {}; // [-] - x = axis, y = slice, z = stride, w = rampMode
    float    ScaleMaxTimeCnt[4] = {}; // [-] - x = arrowScale, y = maxForce, z = simTime, w = activeForceCount
};

//------------------------------------------------------------------------------------------------------------------------
//                                             1:1 GPU GIZMO GEOMETRY BUILDER
//------------------------------------------------------------------------------------------------------------------------

/// Appends 3D line-list vertices (pairs of endpoints) for one ForceModelNode (VolumeRenderer.js L575-L994 1:1 port).
void AppendForceGizmoLineVertices(
    const ForceModelNode& node,
    bool selected,
    float opacity,
    float simulationSeconds,
    const std::array<float, 3>& regionHalfExtent,
    std::vector<ForceGizmoVertex>& outVertices);

/// Builds the combined GPU line-list vertex array for all visible nodes in a register.
void BuildVisibleForceGizmoVertices(
    const ForceFieldRegister& forceRegister,
    std::string_view selectedNodeId,
    float opacity,
    double simulationSeconds,
    const std::array<float, 3>& regionHalfExtent,
    std::vector<ForceGizmoVertex>& outVertices);

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCEFIELDGPUUPLOADER
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldGpuUploader
{
public:
    static constexpr uint32_t kRingSlotCount          = 3u;     // [-] - Triple-buffered upload ring slots
    static constexpr uint32_t kMaxGizmoLineVertices   = 65536u; // [-] - Maximum GPU gizmo line vertices per ring slot

    ForceFieldGpuUploader() noexcept = default;
    ~ForceFieldGpuUploader() noexcept;

    ForceFieldGpuUploader(const ForceFieldGpuUploader&)            = delete;
    ForceFieldGpuUploader& operator=(const ForceFieldGpuUploader&) = delete;

#if FRONTIER_FORCEFIELD_HAS_VULKAN
    [[nodiscard]] bool InitializeUploader(GeometricRaster::VulkanInterchange* boundDevice) noexcept;
    void TeardownUploader() noexcept;

    /// Uploads the latest published ForceFieldRegister reading into the active Vulkan SSBO ring slot.
    [[nodiscard]] uint32_t UploadLatestForceBuffer(const ForceFieldRegister& forceRegister) noexcept;

    /// Uploads CPU-built GPU line vertices into the active Vulkan vertex buffer ring slot.
    [[nodiscard]] uint32_t UploadGizmoVertices(std::span<const ForceGizmoVertex> vertices) noexcept;

    void AdvanceRingSlot() noexcept { ActiveSlotIndex = (ActiveSlotIndex + 1u) % kRingSlotCount; }

    [[nodiscard]] VkBuffer QueryActiveForceSsbo() const noexcept { return ForceSsboRing[ActiveSlotIndex].BufferHandle; }
    [[nodiscard]] VkBuffer QueryActiveGizmoVertexBuffer() const noexcept { return GizmoVertexRing[ActiveSlotIndex].BufferHandle; }
#else
    void AdvanceRingSlot() noexcept { ActiveSlotIndex = (ActiveSlotIndex + 1u) % kRingSlotCount; }
#endif

    [[nodiscard]] uint32_t QueryActiveSlotIndex() const noexcept { return ActiveSlotIndex; }
    [[nodiscard]] uint32_t QueryUploadedForceCount() const noexcept { return UploadedForceCount; }
    [[nodiscard]] uint32_t QueryUploadedGizmoVertexCount() const noexcept { return UploadedGizmoVertexCount; }
    [[nodiscard]] uint64_t QueryLastUploadedGeneration() const noexcept { return LastUploadedGeneration; }

private:
#if FRONTIER_FORCEFIELD_HAS_VULKAN
    GeometricRaster::VulkanInterchange*                           BoundDevice     = nullptr;
    std::array<GeometricRaster::DeviceBufferResource, kRingSlotCount> ForceSsboRing   {};
    std::array<GeometricRaster::DeviceBufferResource, kRingSlotCount> GizmoVertexRing {};
#endif
    uint32_t ActiveSlotIndex          = 0u;
    uint32_t UploadedForceCount       = 0u;
    uint32_t UploadedGizmoVertexCount = 0u;
    uint64_t LastUploadedGeneration   = 0ull;
};

} // namespace Frontier::SimulationSpace::ForceFields
