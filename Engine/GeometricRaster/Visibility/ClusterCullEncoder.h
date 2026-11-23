/*============================================================================================================================================
                                                             CLUSTERCULLENCODER.H
============================================================================================================================================*/
// 🧩 Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t DistanceShapeBakedVolume   = 0u;      // [-]   - Sampled from 3D LocalSignedDistanceVolume grid
inline constexpr uint32_t DistanceShapeBox           = 1u;      // [-]   - Exact analytical oriented box primitive
inline constexpr uint32_t DistanceShapeSphere        = 2u;      // [-]   - Exact analytical sphere primitive
inline constexpr uint32_t DistanceShapeCylinder      = 3u;      // [-]   - Exact analytical vertical cylinder primitive
inline constexpr uint32_t DistanceShapeCone          = 4u;      // [-]   - Exact analytical cone primitive
inline constexpr uint32_t DistanceShapeTorus         = 5u;      // [-]   - Exact analytical torus primitive
inline constexpr uint32_t DistanceShapeExcluded      = 6u;      // [-]   - Thin foliage/scatter excluded from GDF baking

inline constexpr uint32_t InstanceMaterialSlotMask   = 0xFFFFu; // [mask] - Packed 16-bit CompactMaterialRegister slot mask
inline constexpr uint32_t InstanceActiveBitShift     = 16u;     // [idx]  - Bit shift marking active GDF instance entry
inline constexpr uint32_t InstanceGroundSlabBitShift = 17u;     // [idx]  - Bit shift marking analytic ground-plane slab
inline constexpr float    UnsetAnalyticGroundPlaneZ  = -1.0e9f; // [m]    - Inactive datum when no ground slab exists
inline constexpr float    ValidAnalyticGroundFloorZ  = -1.0e8f; // [m]    - Lower validity threshold for ground datum Z

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RigidClusterInstance
{
    const SpatialAcceleration::ClusterPyramid*           PyramidAsset        = nullptr;                  // [-]   - Pre-built 128-triangle cluster LOD pyramid
    Mathematics::Axis3D                                  WorldTranslation    {};                         // [m]   - Active world position
    Mathematics::Axis3D                                  PreviousTranslation {};                         // [m]   - Prior world position (for motion vectors)
    float                                                UniformScale        = 1.0f;                     // [-]   - Uniform rigid world scale
    uint16_t                                             MaterialSlot        = 0u;                       // [idx] - CompactMaterialRegister slot
    bool                                                 TransparentGlass    = false;                    // [-]   - Routed to 2-layer glass depth-peel rasterization
    float                                                RotationZDegrees    = 0.0f;                     // [deg] - Rigid Z-up yaw rotation
    Mathematics::Axis3D                                  NonUniformScale     { 1.0f, 1.0f, 1.0f };       // [-]   - Full 3-axis rigid scale
    Mathematics::Axis3D                                  LocalHalfExtents    { 0.5f, 0.5f, 0.5f };       // [m]   - Unscaled local half-extents / shape radii
    uint32_t                                             AnalyticalShapeCode = DistanceShapeBakedVolume; // [-]   - DistanceShape* volume or analytical primitive tag
    const DistanceFieldSpace::LocalSignedDistanceVolume* LocalSdfVolume      = nullptr;                  // [-]   - Optional per-asset local distance volume
};

struct VisibleClusterCommand
{
    uint32_t InstanceOrdinal  = 0u;   // [idx] - Index into RigidClusterInstance span
    uint32_t ClusterOrdinal   = 0u;   // [idx] - Index into ClusterPyramid::Clusters
    float    ProjectedSpanPx  = 0.0f; // [px]  - Screen-space projected span in pixels
    float    NearestReversedZ = 0.0f; // [0-1] - Front-to-back sort key (1=near, 0=far)
};

struct TwoPhaseCullReport
{
    std::vector<VisibleClusterCommand> HardwareClusterBatches {};   // [>=4 px] - Routed to hardware indirect rasterizer
    std::vector<VisibleClusterCommand> MicroSoftwareClusters  {};   // [<4 px]  - Routed to compute atomicMax64 SW rasterizer
    std::vector<VisibleClusterCommand> TransparentGlassBatches{};   // [glass]  - Routed to 2-layer depth-peel transparency pass
    std::vector<VisibleClusterCommand> PhaseOneDeferredQueue  {};   // [-]      - Occluded in Phase 1; re-tested in Phase 2!
    uint32_t                           TotalLodCutClusters    = 0u; // [count]  - Total clusters selected by continuous LOD cut
    uint32_t                           FrustumRejectedCount   = 0u; // [count]  - Rejected by 6-plane camera frustum
    uint32_t                           NormalConeRejectedCount= 0u; // [count]  - Rejected by cluster normal-cone backface test
    uint32_t                           HiZOccludedCount       = 0u; // [count]  - Rejected by conservative Reversed-Z DepthPyramid
    uint32_t                           PhaseTwoDisoccluded    = 0u; // [count]  - Rescued in Phase 2 after updated Hi-Z build!
};

struct ClusterCullSettings
{
    float MaxAllowedLodErrorPx       = 1.25f; // [px] - Screen-space geometric simplification error threshold
    float MicroClusterThresholdPx    = 4.0f;  // [px] - Clusters smaller than 4 px route to compute atomicMax64 raster
    bool  EnableNormalConeBackface   = true;  // [-]  - Cull clusters whose entire normal cone faces away from camera
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLUSTER CULL ENCODER
//------------------------------------------------------------------------------------------------------------------------

class ClusterCullEncoder
{
public:
    // Phase 1: Select continuous LOD cut, cull against frustum + normal cone + reprojected previous-frame Hi-Z, and split HW vs SW
    [[nodiscard]] static TwoPhaseCullReport EncodePhaseOneVisibility(
        const PerspectiveCameraBasis&         CurrentCamera,
        std::span<const RigidClusterInstance> Instances,
        const DepthPyramidSequence*           PreviousFrameHiZ = nullptr,
        const PerspectiveCameraBasis*         PreviousCamera   = nullptr,
        const ClusterCullSettings&            Settings         = {});

    // Phase 2: Re-test PhaseOneDeferredQueue against freshly built current-frame Hi-Z to rescue newly disoccluded clusters with zero popping!
    static void ResolvePhaseTwoDisocclusions(
        const PerspectiveCameraBasis&         Camera,
        std::span<const RigidClusterInstance> Instances,
        const DepthPyramidSequence&           CurrentFrameHiZ,
        TwoPhaseCullReport&                   InOutReport,
        const ClusterCullSettings&            Settings = {});
};

} // namespace Frontier::GeometricRaster
