/*============================================================================================================================================
                                                               CLUSTERPYRAMID.H
============================================================================================================================================*/
// 🧩 128-triangle hierarchical cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds

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
    Mathematics::Axis3D SphereCenter                {};    // [m]     - Tight bounding sphere center (12 B)
    float               SphereRadiusMeters          = 0.0f; // [m]    - Tight bounding sphere radius (4 B)
    Mathematics::Axis3D BoundsMinimum               {};    // [m]     - World/local AABB lower corner (12 B)
    uint32_t            TriangleStartOffset         = 0u;  // [idx]   - Offset into PyramidTriangles (4 B)
    Mathematics::Axis3D BoundsMaximum               {};    // [m]     - World/local AABB upper corner (12 B)
    uint16_t            TriangleCount               = 0u;  // [count] - Triangles in this cluster (<= 128) (2 B)
    uint8_t             HierarchyLevel              = 0u;  // [level] - 0 = finest source triangles; >0 = simplified LOD (1 B)
    uint8_t             DominantMaterialOrdinal     = 0u;  // [idx]   - Primary material slot inside cluster (1 B)
    Mathematics::Axis3D NormalConeDirection         { 0.0f, 1.0f, 0.0f }; // [-] - Mean unit cluster normal axis (12 B)
    float               NormalConeSinCutoff         = 1.0f; // [-]    - Backface cull sin(theta) threshold; >=1 disables (4 B)
    float               FinerSelfErrorMeters        = 0.0f; // [m]    - Geometric error introduced by this cluster's LOD
    float               CoarserThresholdErrorMeters = 1.0e30f; // [m] - Monotonic error at which coarser level replaces this
    uint64_t            ReservedAlignmentPadding    = 0ull; // [-]    - Explicit pad to 128 B alignment multiple
    uint64_t            ReservedAlignmentPaddingB   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingC   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingD   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingE   = 0ull; // [-]    - Explicit pad
    uint32_t            EnclosingGroupOrdinal       = 0u;   // [idx]  - Coarser merge group ordinal
    uint32_t            ClusterOrdinal              = 0u;   // [idx]  - Self index in Clusters array
};

static_assert(sizeof(ClusterHeaderEntry) == 128u, "ClusterHeaderEntry must occupy exactly 128 aligned bytes.");

struct ClusterCutSelection
{
    std::vector<uint32_t> ActiveClusterIndices {}; // [idx]   - Crack-free active cluster ordinals for current camera
    uint32_t              ActiveTriangleCount  = 0u; // [cnt] - Sum of triangles across selected clusters
    uint32_t              CulledBackfaceCount  = 0u; // [cnt] - Clusters rejected by normal-cone backface test
};

// Per-asset LOD bake recipe (Nanite-like): how deep to bake, and the coarsest level the runtime
// cut may ever select for this asset (e.g. glass keeps level 1 so its silhouette never melts).
struct ClusterLodBakeConfiguration
{
    uint32_t MaximumLevels          = 4u;          // [lvl] - Bake depth incl. level 0 (clamped 1..8)
    uint32_t CoarsestAllowedLevel   = 0xFFFFFFFFu; // [lvl] - Last selectable LOD level (0 = finest only)
    bool     LockMaterialBoundaries = true;        // [-]   - Never collapse vertices shared by faces of different material slots
};

struct ClusterLodLevelSummary
{
    uint32_t              Level               = 0u; // [lvl] - Hierarchy level (0 = finest)
    uint32_t              ClusterCount        = 0u; // [cnt] - Clusters at this level
    uint32_t              TriangleCount       = 0u; // [cnt] - Triangles at this level
    float                 MaximumErrorMeters  = 0.0f; // [m] - Worst FinerSelfError at this level
    std::vector<uint16_t> MaterialOrdinals    {};   // [idx] - Distinct per-face material slots present at this level
};

class ClusterPyramid
{
public:
    std::vector<Mathematics::Axis3D>  Positions        {}; // [m]     - Shared vertex positions across LOD levels
    std::vector<ClusterTriangleEntry> Triangles        {}; // [-]     - Contiguous clustered triangles across all levels
    std::vector<ClusterHeaderEntry>   Clusters         {}; // [-]     - 128 B GPU-aligned cluster descriptors
    uint32_t                          HierarchyDepth   = 0u; // [cnt] - Number of built LOD levels (1..N)
    uint32_t                          FinestClusterCount = 0u; // [cnt] - Level-0 cluster count
    uint32_t                          CoarsestAllowedLevel = 0xFFFFFFFFu; // [lvl] - Runtime cut cap from the bake recipe
    bool                              MaterialBoundariesLocked = true;    // [-]   - Bake kept per-face material slots intact

    void Reset() noexcept;

    // Partitions PolygonNetwork into <=128-triangle Morton clusters and builds seam/border-locked QEM LOD levels.
    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        uint32_t                             MaxHierarchyLevels = 4u);

    // Bake with a full per-asset recipe (bake depth, runtime coarsest level, material-boundary locking).
    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        const ClusterLodBakeConfiguration&   BakeConfiguration);

    // Sets the coarsest LOD level SelectMonotonicCut may ever activate (per asset, e.g. glass).
    void ConfigureCoarsestAllowedLevel(uint32_t Level) noexcept { CoarsestAllowedLevel = Level; }

    // Per-level bake report: clusters, triangles, worst error, and the material slots still present.
    [[nodiscard]] std::vector<ClusterLodLevelSummary> SummarizeLevels() const;

    // Selects a crack-free monotonic LOD cut where FinerSelfError <= AllowedError < CoarserThresholdError.
    [[nodiscard]] ClusterCutSelection SelectMonotonicCut(
        Mathematics::Axis3D CameraWorldPosition,
        float               PixelsPerRadian,
        float               MaxAllowedPixelError = 1.25f,
        bool                EnableNormalConeBackfaceCull = false) const;

    [[nodiscard]] std::vector<std::byte> SerializeToBinaryBytes() const;
    [[nodiscard]] bool                   DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes);
};

} // namespace Frontier::SpatialAcceleration
