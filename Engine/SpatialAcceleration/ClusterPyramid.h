/*============================================================================================================================================
                                                               CLUSTERPYRAMID.H
============================================================================================================================================*/
// 🧩 128-triangle hierarchical cluster LOD pyramid with partition-exact monotonic group cuts and material-isolated QEM

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxTrianglesPerCluster = 128u; // [count] - Hardware/compute visibility dispatch granularity

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ClusterTriangleEntry
{
    uint32_t            VertexIndex0      = 0u;   // [idx] - First PolygonNetwork or simplified vertex index
    uint32_t            VertexIndex1      = 0u;   // [idx] - Second PolygonNetwork or simplified vertex index
    uint32_t            VertexIndex2      = 0u;   // [idx] - Third PolygonNetwork or simplified vertex index
    uint32_t            SourceFaceIndex   = 0u;   // [idx] - Originating PolygonNetwork face index
    Mathematics::UVAxes CornerUV0         {};     // [uv]  - Parameterized UV at corner 0
    Mathematics::UVAxes CornerUV1         {};     // [uv]  - Parameterized UV at corner 1
    Mathematics::UVAxes CornerUV2         {};     // [uv]  - Parameterized UV at corner 2
    uint32_t            PackedNormal0     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 0
    uint32_t            PackedNormal1     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 1
    uint32_t            PackedNormal2     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 2
    uint32_t            PackedTangent0    = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner tangent 0
    uint16_t            MaterialOrdinal   = 0u;   // [idx] - PartGroupOrdinal / MaterialSlot assignment
    int16_t             TangentSignScaled = 1;    // [-]   - +1 or -1 bitangent handedness sign
};

struct alignas(64) ClusterHeaderEntry
{
    Mathematics::Axis3D SphereCenter                {};     // [m]     - Tight bounding sphere center (12 B)
    float               SphereRadiusMeters          = 0.0f; // [m]     - Tight bounding sphere radius (4 B)
    Mathematics::Axis3D BoundsMinimum               {};     // [m]     - Local AABB lower corner (12 B)
    uint32_t            TriangleStartOffset         = 0u;   // [idx]   - Offset into Triangles (4 B)
    Mathematics::Axis3D BoundsMaximum               {};     // [m]     - Local AABB upper corner (12 B)
    uint16_t            TriangleCount               = 0u;   // [count] - Triangles in this cluster (<= 128) (2 B)
    uint8_t             HierarchyLevel              = 0u;   // [level] - 0 = finest source triangles; >0 = simplified LOD (1 B)
    uint8_t             DominantMaterialOrdinal     = 0u;   // [idx]   - Low 8-bit material slot inside cluster (1 B)
    Mathematics::Axis3D NormalConeDirection         { 0.0f, 1.0f, 0.0f }; // [-] - Mean unit cluster normal axis (12 B)
    float               NormalConeSinCutoff         = 1.0f; // [-]     - Backface cull sin(theta) threshold; >=1 disables (4 B)
    float               FinerSelfErrorMeters        = 0.0f; // [m]     - Monotonic error of OriginGroupOrdinal (0 at level 0) (4 B)
    float               CoarserThresholdErrorMeters = 1.0e30f; // [m]  - Monotonic error of EnclosingGroupOrdinal (4 B)
    Mathematics::Axis3D OriginGroupSphereCenter     {};     // [m]     - Shared monotonic sphere center of generating group (12 B)
    float               OriginGroupSphereRadius     = 0.0f; // [m]     - Shared monotonic sphere radius of generating group (4 B)
    Mathematics::Axis3D EnclosingGroupSphereCenter  {};     // [m]     - Shared monotonic sphere center of replacing group (12 B)
    float               EnclosingGroupSphereRadius  = 0.0f; // [m]     - Shared monotonic sphere radius of replacing group (4 B)
    uint32_t            OriginGroupOrdinal          = 0u;   // [idx]   - Merge group that generated this cluster (0 = leaf) (4 B)
    uint16_t            FullMaterialOrdinal         = 0u;   // [idx]   - Exact 16-bit per-cluster material slot (2 B)
    uint16_t            EnclosingOutputLevel        = 0u;   // [lvl]   - Hierarchy level produced by EnclosingGroupOrdinal (2 B)
    uint32_t            EnclosingGroupOrdinal       = 0u;   // [idx]   - Coarser merge group ordinal replacing this cluster (4 B)
    uint32_t            ClusterOrdinal              = 0u;   // [idx]   - Self index in Clusters array (4 B)
};

static_assert(sizeof(ClusterHeaderEntry) == 128u, "ClusterHeaderEntry must occupy exactly 128 aligned bytes.");

struct ClusterCutSelection
{
    std::vector<uint32_t> ActiveClusterIndices {};   // [idx] - Partition-exact active cluster ordinals for current camera
    uint32_t              ActiveTriangleCount  = 0u; // [cnt] - Sum of triangles across selected clusters
    uint32_t              CulledBackfaceCount  = 0u; // [cnt] - Clusters rejected by normal-cone backface test
};

struct ClusterLodBakeConfiguration
{
    uint32_t MaximumLevels          = 4u;          // [lvl] - Bake depth including level 0 (clamped 1..8)
    uint32_t CoarsestAllowedLevel   = 0xFFFFFFFFu; // [lvl] - Coarsest selectable LOD level for the asset (0 = finest only)
    bool     LockMaterialBoundaries = true;        // [-]   - Never collapse vertices shared across distinct material slots
};

struct ClusterLodLevelSummary
{
    uint32_t              Level              = 0u;   // [lvl] - Hierarchy level (0 = finest)
    uint32_t              ClusterCount       = 0u;   // [cnt] - Clusters at this level
    uint32_t              TriangleCount      = 0u;   // [cnt] - Triangles at this level
    float                 MaximumErrorMeters = 0.0f; // [m]   - Worst FinerSelfError at this level
    std::vector<uint16_t> MaterialOrdinals   {};     // [idx] - Distinct per-face material slots present at this level
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CLUSTER PYRAMID
//------------------------------------------------------------------------------------------------------------------------

class ClusterPyramid
{
public:
    std::vector<Mathematics::Axis3D>  Positions                {};          // [m]   - Shared vertex positions across LOD levels
    std::vector<ClusterTriangleEntry> Triangles                {};          // [-]   - Contiguous clustered triangles across all levels
    std::vector<ClusterHeaderEntry>   Clusters                 {};          // [-]   - 128 B GPU-aligned cluster descriptors
    std::vector<uint32_t>             MaterialCoarsestLevels   {};          // [lvl] - Per-material-slot coarsest selectable LOD level
    uint32_t                          HierarchyDepth           = 0u;        // [cnt] - Number of built LOD levels (1..N)
    uint32_t                          FinestClusterCount       = 0u;        // [cnt] - Level-0 cluster count
    uint32_t                          CoarsestAllowedLevel     = 0xFFFFFFFFu;// [lvl]- Asset-wide runtime coarsest LOD cap
    bool                              MaterialBoundariesLocked = true;      // [-]   - Bake kept per-face material slots isolated

    void Reset() noexcept;

    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        uint32_t                             MaxHierarchyLevels = 4u);

    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        const ClusterLodBakeConfiguration&   BakeConfiguration);

    void ConfigureCoarsestAllowedLevel(uint32_t Level) noexcept;

    void ConfigureMaterialCoarsestLevel(uint16_t MaterialOrdinal, uint32_t CoarsestLevel);

    [[nodiscard]] uint32_t QueryMaterialCoarsestLevel(uint16_t MaterialOrdinal) const noexcept;

    [[nodiscard]] std::vector<ClusterLodLevelSummary> SummarizeLevels() const;

    [[nodiscard]] ClusterCutSelection SelectMonotonicCut(
        Mathematics::Axis3D CameraWorldPosition,
        float               PixelsPerRadian,
        float               MaxAllowedPixelError = 1.25f,
        bool                EnableNormalConeBackfaceCull = false) const;

    [[nodiscard]] ClusterCutSelection SelectMonotonicCutWithCap(
        Mathematics::Axis3D CameraWorldPosition,
        float               PixelsPerRadian,
        float               MaxAllowedPixelError,
        uint32_t            CoarsestLevelOverride,
        bool                EnableNormalConeBackfaceCull = false) const;

    [[nodiscard]] std::vector<std::byte> SerializeToBinaryBytes() const;
    [[nodiscard]] bool                   DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes);
};

//------------------------------------------------------------------------------------------------------------------------
//                                               INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void ClusterPyramid::ConfigureCoarsestAllowedLevel(uint32_t Level) noexcept
{
    CoarsestAllowedLevel = Level;
}

inline uint32_t ClusterPyramid::QueryMaterialCoarsestLevel(uint16_t MaterialOrdinal) const noexcept
{
    if (MaterialOrdinal < MaterialCoarsestLevels.size())
    {
        return MaterialCoarsestLevels[MaterialOrdinal];
    }
    return 0xFFFFFFFFu;
}

} // namespace Frontier::SpatialAcceleration
