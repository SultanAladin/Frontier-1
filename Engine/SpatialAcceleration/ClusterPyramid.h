/*============================================================================================================================================
                                                               CLUSTERPYRAMID.H
============================================================================================================================================*/
// 🧩 128-triangle hierarchical DAG cluster pyramid (GeometryCompiler architecture) with UV, material & normal preservation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstddef>
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

struct SymmetricQuadric10
{
    double C[10] {}; // [m^2] - Upper-triangular symmetric 4x4 plane quadric coefficients

    [[nodiscard]] static SymmetricQuadric10 FromPlane(double A, double B, double D, double OffsetW, double Weight = 1.0) noexcept
    {
        SymmetricQuadric10 Quad {};
        Quad.C[0] = Weight * A * A;
        Quad.C[1] = Weight * A * B;
        Quad.C[2] = Weight * A * D;
        Quad.C[3] = Weight * A * OffsetW;
        Quad.C[4] = Weight * B * B;
        Quad.C[5] = Weight * B * D;
        Quad.C[6] = Weight * B * OffsetW;
        Quad.C[7] = Weight * D * D;
        Quad.C[8] = Weight * D * OffsetW;
        Quad.C[9] = Weight * OffsetW * OffsetW;
        return Quad;
    }

    SymmetricQuadric10& operator+=(const SymmetricQuadric10& Other) noexcept
    {
        for (uint32_t Idx = 0u; Idx < 10u; ++Idx)
        {
            C[Idx] += Other.C[Idx];
        }
        return *this;
    }

    [[nodiscard]] double EvaluatePoint(Mathematics::Axis3D P) const noexcept
    {
        const double X = static_cast<double>(P.X);
        const double Y = static_cast<double>(P.Y);
        const double Z = static_cast<double>(P.Z);
        return C[0] * X * X + 2.0 * C[1] * X * Y + 2.0 * C[2] * X * Z + 2.0 * C[3] * X +
               C[4] * Y * Y + 2.0 * C[5] * Y * Z + 2.0 * C[6] * Y +
               C[7] * Z * Z + 2.0 * C[8] * Z + C[9];
    }
};

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
    Mathematics::Axis3D SphereCenter                {};    // [m]     - Tight cluster bounding sphere center (12 B)
    float               SphereRadiusMeters          = 0.0f; // [m]    - Tight cluster bounding sphere radius (4 B)
    Mathematics::Axis3D BoundsMinimum               {};    // [m]     - Cluster AABB lower corner (12 B)
    uint32_t            TriangleStartOffset         = 0u;  // [idx]   - Offset into Triangles array (4 B)
    Mathematics::Axis3D BoundsMaximum               {};    // [m]     - Cluster AABB upper corner (12 B)
    uint16_t            TriangleCount               = 0u;  // [count] - Triangles in this cluster (<= 128) (2 B)
    uint8_t             HierarchyLevel              = 0u;  // [level] - 0 = finest source triangles; >0 = simplified LOD (1 B)
    uint8_t             DominantMaterialOrdinal     = 0u;  // [idx]   - Primary material slot inside cluster (1 B)
    Mathematics::Axis3D NormalConeDirection         { 0.0f, 1.0f, 0.0f }; // [-] - Mean unit cluster normal axis (12 B)
    float               NormalConeSinCutoff         = 1.0f; // [-]    - Backface cull sin(theta) threshold; >=1 disables (4 B)
    float               FinerSelfErrorMeters           = 0.0f; // [m]    - Monotone group error introduced by producing group (4 B)
    float               CoarserThresholdErrorMeters    = 1.0e30f; // [m] - Monotone group error at which coarser group replaces this (4 B)
    Mathematics::Axis3D SelfLodSphereCenter            {};    // [m]     - Shared producing-group sphere center for crack-free cut (12 B)
    float               SelfLodSphereRadiusMeters      = 0.0f; // [m]    - Shared producing-group sphere radius (4 B)
    Mathematics::Axis3D CoarserGroupSphereCenter       {};    // [m]     - Shared coarser-group sphere center for crack-free cut (12 B)
    float               CoarserGroupSphereRadiusMeters = 0.0f; // [m]    - Shared coarser-group sphere radius (4 B)
    uint32_t            ProducingGroupOrdinal       = 0u;  // [idx]   - Simplification group that produced this cluster (4 B)
    uint32_t            EnclosingGroupOrdinal       = 0u;  // [idx]   - Simplification group merging this cluster upward (4 B)
    uint32_t            ClusterOrdinal              = 0u;  // [idx]   - Self index in Clusters array (4 B)
    uint32_t            ReservedAlignmentWords[3]   {};    // [-]     - Explicit pad to exact 128-byte aligned footprint (12 B)
};

static_assert(sizeof(ClusterHeaderEntry) == 128u, "ClusterHeaderEntry must occupy exactly 128 aligned bytes.");

struct ClusterCutSelection
{
    std::vector<uint32_t>     ActiveClusterIndices {}; // [idx] - Crack-free active cluster ordinals for current camera
    uint32_t                  ActiveTriangleCount  = 0u; // [cnt] - Sum of triangles across selected clusters
    uint32_t                  CulledBackfaceCount  = 0u; // [cnt] - Clusters rejected by normal-cone backface test
    std::array<uint32_t, 16u> ClustersByLevel      {}; // [cnt] - Selected cluster histogram per LOD level
    std::array<uint32_t, 16u> TrianglesByLevel     {}; // [cnt] - Selected triangle histogram per LOD level
};

struct ClusterLodBakeConfiguration
{
    uint32_t               MaximumLevels             = 6u;          // [lvl] - Bake depth incl. level 0 (clamped 1..12)
    uint32_t               CoarsestAllowedLevel      = 0xFFFFFFFFu; // [lvl] - Last selectable LOD level (0 = finest only)
    bool                   LockMaterialBoundaries    = true;        // [-]   - Preserve per-face material slots & seam geometry
    bool                   LockUvSeamBoundaries      = true;        // [-]   - Preserve UV island seams & parameterization
    float                  CreaseAngleDegrees        = 55.0f;       // [deg] - Hard-edge crease protection angle
    std::span<const float> AuthoredFaceCornerNormals {};            // [-]   - Optional FaceCount * 9 per-corner smooth normals
};

struct ClusterLodLevelSummary
{
    uint32_t              Level               = 0u;   // [lvl] - Pyramid level (0 = finest)
    uint32_t              ClusterCount        = 0u;   // [cnt] - Clusters at this level
    uint32_t              TriangleCount       = 0u;   // [cnt] - Triangles at this level
    float                 MaximumErrorMeters  = 0.0f; // [m]   - Worst FinerSelfError at this level
    std::vector<uint16_t> MaterialOrdinals    {};     // [idx] - Distinct per-face material slots present at this level
};

class ClusterPyramid
{
public:
    std::vector<Mathematics::Axis3D>  Positions                {}; // [m]   - Shared welded vertex positions across LOD levels
    std::vector<ClusterTriangleEntry> Triangles                {}; // [-]   - Contiguous clustered triangles across all levels
    std::vector<ClusterHeaderEntry>   Clusters                 {}; // [-]   - 128 B GPU-aligned cluster descriptors
    uint32_t                          HierarchyDepth           = 0u; // [cnt] - Number of built LOD levels (1..N)
    uint32_t                          FinestClusterCount       = 0u; // [cnt] - Level-0 cluster count
    uint32_t                          CoarsestAllowedLevel     = 0xFFFFFFFFu; // [lvl] - Runtime cut cap from bake recipe
    bool                              MaterialBoundariesLocked = true; // [-] - Bake kept per-face material slots intact

    void Reset() noexcept;

    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        uint32_t                             MaxHierarchyLevels = 6u);

    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        const ClusterLodBakeConfiguration&   BakeConfiguration);

    void ConfigureCoarsestAllowedLevel(uint32_t Level) noexcept { CoarsestAllowedLevel = Level; }

    void ConfigureMaterialCoarsestAllowedLevel(uint8_t MaterialOrdinal, uint32_t MaxLevel) noexcept
    {
        MaterialCoarsestAllowedLevels[MaterialOrdinal] = MaxLevel;
    }

    std::array<uint32_t, 256u> MaterialCoarsestAllowedLevels = []() constexpr
    {
        std::array<uint32_t, 256u> LevelCaps {};
        LevelCaps.fill(0xFFFFFFFFu);
        return LevelCaps;
    }();

    [[nodiscard]] std::vector<ClusterLodLevelSummary> SummarizeLevels() const;

    // Selects a crack-free monotonic DAG cut using shared group spheres & errors (GeometryCompiler parity).
    [[nodiscard]] ClusterCutSelection SelectMonotonicCut(
        Mathematics::Axis3D CameraWorldPosition,
        float               PixelsPerRadian,
        float               MaxAllowedPixelError         = 1.25f,
        bool                EnableNormalConeBackfaceCull = false,
        int32_t             ForcedPyramidLevel           = -1) const;

    [[nodiscard]] std::vector<std::byte> SerializeToBinaryBytes() const;
    [[nodiscard]] bool                   DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes);
};

// Octahedral SNORM16x2 codecs shared with runtime ray/visibility builders.
[[nodiscard]] uint32_t            PackClusterOctahedralUnitVector(Mathematics::Axis3D UnitDir) noexcept;
[[nodiscard]] Mathematics::Axis3D UnpackClusterOctahedralUnitVector(uint32_t PackedWord) noexcept;

} // namespace Frontier::SpatialAcceleration
