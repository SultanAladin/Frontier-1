/*============================================================================================================================================
                                                            VIEWPORTPICKQUERY.H
============================================================================================================================================*/
// 🧩 Unified 3D & 2D GPU R32G32B32A32_UINT visibility pick query supporting 1x1 cursor readback and marquee/brush VRAM reduction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <filesystem>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportPickHit
{
    uint32_t InstanceSlot       = 0u;                                  // [idx] - 0 = background / ground grid; >= 1 = GeometryInstance slot
    uint32_t FaceIndex          = GeometrySpace::InvalidNetworkIndex;  // [idx] - Hit PolygonNetwork Face index
    uint32_t DirectedEdgeIndex  = GeometrySpace::InvalidNetworkIndex;  // [idx] - Closest DirectedEdge index
    uint32_t GizmoHandleOrdinal = 0u;                                  // [idx] - > 0 when cursor hits a 3D or 2D TransformGizmo handle

    [[nodiscard]] constexpr bool HitGizmo() const noexcept
    {
        return GizmoHandleOrdinal != 0u;
    }

    [[nodiscard]] constexpr bool HitSurfaceFace() const noexcept
    {
        return InstanceSlot != 0u && FaceIndex != GeometrySpace::InvalidNetworkIndex;
    }
};

struct RegionPickSummary
{
    uint32_t HitPixelCount      = 0u; // [px]  - Number of surface pixels inside marquee/brush region
    uint32_t FirstInstanceSlot  = 0u; // [idx] - Highest non-zero InstanceSlot touched
    uint32_t FirstGizmoOrdinal  = 0u; // [idx] - Highest GizmoHandleOrdinal touched
};

class ViewportPickQuery
{
public:
    ViewportPickQuery() = default;
    ~ViewportPickQuery();

    ViewportPickQuery(const ViewportPickQuery&)            = delete;
    ViewportPickQuery& operator=(const ViewportPickQuery&) = delete;

    [[nodiscard]] bool InitializePickQuery(
        VulkanInterchange*         BoundDevice,
        ComputeDispatchEncoder*      BoundComputeEncoder,
        TimelineTransferRegister*           BoundTransferRegister,
        const std::filesystem::path& PickReductionSpirvPath);

    void TeardownPickQuery() noexcept;

    [[nodiscard]] ViewportPickHit QuerySinglePixel(
        VkImage       PickAttachmentImage,
        VkImageLayout CurrentLayout,
        uint32_t      ImageWidthPx,
        uint32_t      ImageHeightPx,
        int32_t       CursorX,
        int32_t       CursorY);

    [[nodiscard]] RegionPickSummary ExecuteRegionReduction(
        VkImage       PickAttachmentImage,
        VkImageLayout CurrentLayout,
        uint32_t      ImageWidthPx,
        uint32_t      ImageHeightPx,
        uint32_t      RegionMinX,
        uint32_t      RegionMinY,
        uint32_t      RegionMaxX,
        uint32_t      RegionMaxY,
        uint32_t      BrushCenterX,
        uint32_t      BrushCenterY,
        uint32_t      BrushRadiusPx,
        VkBuffer      TargetSelectionBitmaskBuffer,
        uint32_t      MaxFaceCount);

private:
    void EnsureFlattenedBufferCapacity(uint64_t RequiredBytes);

    VulkanInterchange*    DeviceInterchange                = nullptr;    // [-] - Vulkan device interchange
    ComputeDispatchEncoder* ComputeEncoder         = nullptr;    // [-] - Modular compute dispatcher
    TimelineTransferRegister*      TransferRegister           = nullptr;    // [-] - Timeline Semaphore readback ring
    uint32_t                ReductionProgramHandle = UINT32_MAX; // [idx] - ViewportPickReduction.comp.spv handle
    DeviceBufferResource    ParamsUniformBuffer    {};           // [-] - PickReductionParams constant buffer
    DeviceBufferResource    FlattenedPickBuffer    {};           // [-] - Width*Height*16B buffer copy for region reduction
    DeviceBufferResource    SummaryStorageBuffer   {};           // [-] - 3*uint32 atomic hit summary buffer
};

} // namespace Frontier::GeometricRaster
