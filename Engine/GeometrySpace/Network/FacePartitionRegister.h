/*============================================================================================================================================
                                                           FACEPARTITIONREGISTER.H
============================================================================================================================================*/
// 🧩 PriorityAllocationLayer & UVRevision-stamped FacePartitionSpan enabling O(1) GPU layer selection and sub-0.05ms CPU spans

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/GeometryInstance.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PriorityAllocationLayer
{
    uint16_t              LayerOrdinal       = 1u;                       // [idx]   - 1-based ordinal stored in Face::PriorityLayerOrdinal
    std::string           LayerTitle         { "Layer 1" };              // [-]     - Display title in UV Space Outliner & Pack Inspector
    Mathematics::Scalar3D LinearTintRGB      { 0.937f, 0.957f, 0.812f }; // [0-1]   - Layer colour (#EFF4CF Studio Ivory default)
    float                 TargetSharePercent = 25.0f;                    // [%]     - Target UDIM space allocation share [0..100]
    uint32_t              TargetUDIMNumber   = 0u;                       // [code]  - 0 = Auto UDIM; >= 1001 = pinned UDIM tile
};

struct FacePartitionSpan
{
    GeometryInstanceToken     OwnerInstance      {};     // [-]     - Bound GeometryInstance handle
    uint16_t                  LayerOrdinal       = 0u;   // [idx]   - 0 = Unassigned Remainder; 1..N = PriorityAllocationLayer
    uint64_t                  CapturedUVRevision = 0ull; // [count] - PolygonNetwork::UVRevision when spans were indexed
    std::vector<uint32_t>     FaceIndices        {};     // [idx]   - Contiguous sorted Face indices in this layer
    std::vector<uint32_t>     UDIMNumbers        {};     // [code]  - Sorted distinct UDIM tile codes touched by this layer
    float                     TotalArea3D        = 0.0f; // [m^2]   - Cached sum of 3D face areas (eliminates click loops!)
    float                     TotalAreaUV        = 0.0f; // [uv^2]  - Cached sum of unsigned UV face areas
    Mathematics::UVAxes       UVBoundsMin        {};     // [uv]    - Lower UV bounding corner
    Mathematics::UVAxes       UVBoundsMax        {};     // [uv]    - Upper UV bounding corner
};

class FacePartitionRegister
{
public:
    FacePartitionRegister() = default;

    [[nodiscard]] uint16_t ConstructLayer(
        std::string_view      Title,
        Mathematics::Scalar3D LinearTintRGB,
        float                 TargetSharePercent,
        uint32_t              TargetUDIMNumber = 0u);

    bool RemoveLayer(PolygonNetwork& Network, uint16_t LayerOrdinal);
    void AssignFacesToLayer(
        PolygonNetwork&           Network,
        std::span<const uint32_t> TargetFaceIndices,
        uint16_t                  LayerOrdinal) noexcept;

    void SynchronizePartitions(
        const PolygonNetwork& Network,
        GeometryInstanceToken OwnerInstance = {});

    [[nodiscard]] const FacePartitionSpan& InspectPartitionSpan(
        const PolygonNetwork& Network,
        uint16_t              LayerOrdinal,
        GeometryInstanceToken OwnerInstance = {});

    [[nodiscard]] PriorityAllocationLayer*                  ResolveLayer(uint16_t LayerOrdinal) noexcept;
    [[nodiscard]] const PriorityAllocationLayer*            InspectLayer(uint16_t LayerOrdinal) const noexcept;
    [[nodiscard]] std::span<const PriorityAllocationLayer>  InspectLayers() const noexcept;
    [[nodiscard]] float                                     DeriveExplicitShareSum() const noexcept;
    [[nodiscard]] float                                     DeriveRemainderSharePercent() const noexcept;

private:
    std::vector<PriorityAllocationLayer> Layers                {};   // [-]     - Active user-created UV priority layers
    std::vector<FacePartitionSpan>      IndexedPartitions     {};   // [-]     - Slot 0 = Remainder; Slot k = LayerOrdinal k
    uint64_t                             SynchronizedRevision  = 0ull; // [count] - Last synchronized PolygonNetwork::UVRevision
    uint16_t                             NextLayerOrdinal      = 1u; // [idx]   - Monotonic layer ordinal generator
};

} // namespace Frontier::GeometrySpace
