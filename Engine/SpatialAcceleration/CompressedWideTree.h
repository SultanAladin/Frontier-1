/*============================================================================================================================================
                                                            COMPRESSEDWIDETREE.H
============================================================================================================================================*/
// 🧩 64-byte 8-bit quantized 4-wide SAH BVH4 (Ylitie-Karras-Laine 2017 DP collapse), multi-instance TLAS4+BLAS4, and fast AABB refit

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t CompressedEmptySlotLink     = 0xFFFFFFFFu; // [idx] - Unused branch link sentinel
inline constexpr uint32_t CompressedLeafPrimCeiling   = 15u;         // [cnt] - Maximum triangles per compressed leaf
inline constexpr uint32_t CompressedQuantizationSteps = 255u;        // [-]   - 8-bit quantized subtree bounds

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact 64-byte (16 x uint32_t) 8-bit quantized 4-wide BVH4 node shared across CPU and .slang GPU ray queries
struct alignas(64) CompressedWideEntry64
{
    float    OriginX          = 0.0f; // [0]  - Shared lower X origin for 4 subtrees
    float    OriginY          = 0.0f; // [1]  - Shared lower Y origin for 4 subtrees
    float    OriginZ          = 0.0f; // [2]  - Shared lower Z origin for 4 subtrees
    uint32_t PackedExponents  = 0u;   // [3]  - (ExpX+127) | ((ExpY+127)<<8) | ((ExpZ+127)<<16) | (ActiveBranches<<24)
    uint32_t QuantizedBounds[6] {};   // [4..9] - 24 uint8s: minX[4], maxX[4], minY[4], maxY[4], minZ[4], maxZ[4]
    uint32_t PackedCounts01   = 0u;   // [10] - Count0 | (Count1 << 16)  (0 = internal subtree; >0 = leaf count)
    uint32_t PackedCounts23   = 0u;   // [11] - Count2 | (Count3 << 16)
    uint32_t SubtreeLinks[4]  {       // [12..15] - Subtree node index (when Count==0) or FirstPrim (when Count>0)
        CompressedEmptySlotLink,
        CompressedEmptySlotLink,
        CompressedEmptySlotLink,
        CompressedEmptySlotLink,
    };
};

static_assert(sizeof(CompressedWideEntry64) == 64u, "CompressedWideEntry64 must occupy 64 aligned bytes.");

// 📝 Compact 32-byte rigid yaw+translation instance record for 50+ vehicle fleet & multi-asset TLAS4 traversal
struct alignas(16) RigidInstancePlacement32
{
    float    CosineYaw         = 1.0f;     // [-]   - cos(yaw) around vertical axis
    float    SineYaw           = 0.0f;     // [-]   - sin(yaw) around vertical axis
    float    TranslationA      = 0.0f;     // [m]   - Horizontal translation axis A (X)
    float    TranslationB      = 0.0f;     // [m]   - Horizontal translation axis B (Y in Z-up, Z in Y-up)
    uint32_t BlasRootEntryIndex = 0u;       // [idx] - Root CompressedWideEntry64 offset in shared node buffer
    uint32_t MaterialOverride0 = 0xFFFFu;  // [packed] - Low16 = BodyCoat swap, High16 = RimAlloy swap (0xFFFF = unchanged)
    uint32_t VerticalOffsetBits = 0u;      // [bits]- IEEE-754 float vertical translation (Z in Z-up, Y in Y-up)
    uint32_t AxisConvention    = 0u;       // [0/1] - 0 = Z-up (yaw in XY), 1 = Y-up (yaw in XZ, matches Studio WebGPU)
};

static_assert(sizeof(RigidInstancePlacement32) == 32u, "RigidInstancePlacement32 must occupy 32 bytes.");

// 📝 Flat 32-byte binary BVH node retained alongside deforming meshes for O(N) GPU/CPU bottom-up AABB refit
struct alignas(16) RefittableBinaryEntry32
{
    float    BoundsMin[3]   { 0.0f, 0.0f, 0.0f };
    uint32_t LeftOrFirst    = 0u; // Left binary entry index when PrimCount == 0, else first primitive index
    float    BoundsMax[3]   { 0.0f, 0.0f, 0.0f };
    uint32_t PrimitiveCount = 0u; // 0 = internal binary node (right = LeftOrFirst + 1); >0 = leaf triangle count
};

static_assert(sizeof(RefittableBinaryEntry32) == 32u, "RefittableBinaryEntry32 must occupy 32 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMPRESSED WIDE TREE
//------------------------------------------------------------------------------------------------------------------------

class CompressedWideTree
{
public:
    std::vector<CompressedWideEntry64>    WideEntries            {}; // [-] - Contiguous 64 B quantized 4-wide nodes
    std::vector<RayTrianglePrimitive>    Primitives       {}; // [-] - Reordered contiguous leaf triangle stream
    std::vector<TriangleSmoothNormals>   PrimitiveNormals {}; // [-] - Reordered smooth normals aligned 1:1 with Primitives
    std::vector<uint32_t>                SourceRemapOrder {}; // [-] - Maps reordered leaf slot -> input triangle index
    std::vector<RefittableBinaryEntry32> BinaryHierarchy  {}; // [-] - Retained binary hierarchy for sub-0.08 ms refit
    std::vector<uint32_t>                BinaryBottomUp   {}; // [-] - Post-order internal node indices for bottom-up refit
    Mathematics::Axis3D                  RootBoundsMin    {};
    Mathematics::Axis3D                  RootBoundsMax    {};
    uint32_t                             MaxWideDepth     = 0u;

    void Reset() noexcept;

    // Builds 32-bin SAH binary hierarchy and collapses via Ylitie-Karras-Laine (2017) DP into 64 B 4-wide quantized nodes
    bool BuildFromPrimitives(
        std::span<const RayTrianglePrimitive>  InputPrimitives,
        std::span<const TriangleSmoothNormals> InputNormals    = {},
        uint32_t                               EntryStartOffset  = 0u,
        uint32_t                               MaxLeafCount    = CompressedLeafPrimCeiling);

    // Refits AABBs across deformed vertex positions in O(N) without full SAH rebuild (< 0.08 ms for character rigs)
    bool RefitDeformedPrimitives(
        std::span<const RayTrianglePrimitive>  UpdatedSourcePrimitives,
        std::span<const TriangleSmoothNormals> UpdatedSourceNormals = {},
        uint32_t                               EntryStartOffset       = 0u);

    [[nodiscard]] RayIntersectionResult TraceClosestRay(
        const RaySegmentQuery& Query,
        bool                   IncludeTransmissive = true,
        uint32_t               RootEntryIndex       = 0u) const noexcept;

    [[nodiscard]] bool QueryRayOccluded(
        const RaySegmentQuery& Query,
        uint32_t               RootEntryIndex = 0u) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPRESSED INSTANCE FOREST
//------------------------------------------------------------------------------------------------------------------------

class CompressedInstanceForest
{
public:
    std::vector<CompressedWideEntry64>  SharedEntries      {}; // [0..TlasCount-1] = TLAS4; [BlasOffset..] = concatenated BLAS4s
    std::vector<RigidInstancePlacement32> Instances        {}; // 1:1 with TLAS leaf links
    std::vector<RayTrianglePrimitive>  SharedPrimitives {};
    uint32_t                           TlasRootIndex    = 0u;

    void Reset() noexcept;

    // Builds a single-BLAS + N-instance TLAS4 forest (e.g. 50-car ControlVehicle fleet + static showroom BLAS)
    bool AssembleInstanceForest(
        const CompressedWideTree&                StaticWorldBlas,
        const CompressedWideTree&                PrototypeBlas,
        std::span<const RigidInstancePlacement32>   PrototypeInstances);

    [[nodiscard]] RayIntersectionResult TraceClosestRay(
        const RaySegmentQuery& Query,
        bool                   IncludeTransmissive = true) const noexcept;

    [[nodiscard]] bool QueryRayOccluded(const RaySegmentQuery& Query) const noexcept;
};

} // namespace Frontier::SpatialAcceleration
