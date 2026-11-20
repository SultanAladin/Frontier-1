/*============================================================================================================================================
                                                              WIDEBOUNDINGTREE.H
============================================================================================================================================*/
// 🧩 Compressed 128-byte 4-Wide SAH BVH4 acceleration tree for deterministic GTX + RTX closest-hit and shadow occlusion queries

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t WideLeafReferenceMask  = 0x80000000u; // [bit] - High bit marks BranchReference as primitive leaf offset
inline constexpr uint32_t EmptyBranchReference   = 0xFFFFFFFFu; // [idx] - Unused branch slot marker

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RayTrianglePrimitive
{
    Mathematics::Axis3D Corner0         {}; // [m]   - First world/local triangle vertex
    uint32_t            SourceFaceIndex = 0u; // [idx] - Originating PolygonNetwork face index
    Mathematics::Axis3D Edge01          {}; // [m]   - Precomputed Corner1 - Corner0 for fast Moller-Trumbore
    uint16_t            MaterialOrdinal = 0u; // [idx] - Assigned material / part ordinal
    uint16_t            OpaqueMask      = 1u; // [bit] - 1 = opaque shadow caster; 0 = transmissive
    Mathematics::Axis3D Edge02          {}; // [m]   - Precomputed Corner2 - Corner0
    uint32_t            PrimitiveOrdinal = 0u; // [idx] - Stable primitive ordinal
};

static_assert(sizeof(RayTrianglePrimitive) == 48u, "RayTrianglePrimitive must occupy 48 compact bytes.");

// 📝 128-byte aligned 4-Wide SIMD/GPU-ready BVH4 internal entry (zero kinship naming!)
struct alignas(64) WideTreeEntry
{
    std::array<float, 4u>    BranchBoundsMinX     {}; // [m]     - Lower X bounds of 4 subtrees (16 B)
    std::array<float, 4u>    BranchBoundsMaxX     {}; // [m]     - Upper X bounds of 4 subtrees (16 B)
    std::array<float, 4u>    BranchBoundsMinY     {}; // [m]     - Lower Y bounds of 4 subtrees (16 B)
    std::array<float, 4u>    BranchBoundsMaxY     {}; // [m]     - Upper Y bounds of 4 subtrees (16 B)
    std::array<float, 4u>    BranchBoundsMinZ     {}; // [m]     - Lower Z bounds of 4 subtrees (16 B)
    std::array<float, 4u>    BranchBoundsMaxZ     {}; // [m]     - Upper Z bounds of 4 subtrees (16 B)
    std::array<uint32_t, 4u> BranchReference      {}; // [idx]   - Internal WideTreeEntry index or (WideLeafReferenceMask | Start) (16 B)
    std::array<uint32_t, 4u> BranchPrimitiveCount {}; // [count] - 0 = internal branch; >0 = leaf triangle count (16 B)
};

static_assert(sizeof(WideTreeEntry) == 128u, "WideTreeEntry must occupy exactly 128 aligned bytes.");

struct RaySegmentQuery
{
    Mathematics::Axis3D Origin            {};        // [m] - Ray origin
    float               MinimumDistance   = 1.0e-4f; // [m] - Near t_min clip distance
    Mathematics::Axis3D UnitDirection     { 0.0f, 0.0f, -1.0f }; // [-] - Normalized ray direction
    float               MaximumDistance   = 1.0e30f; // [m] - Far t_max clip distance
};

struct RayIntersectionResult
{
    float               HitDistanceMeters = 1.0e30f; // [m]   - Parametric distance t along UnitDirection
    float               BarycentricU      = 0.0f;    // [-]   - Weight of Corner1 (Corner0 weight = 1 - U - V)
    float               BarycentricV      = 0.0f;    // [-]   - Weight of Corner2
    Mathematics::Axis3D GeometricNormal   { 0.0f, 1.0f, 0.0f }; // [-] - Outward unit geometric normal
    uint32_t            SourceFaceIndex   = 0xFFFFFFFFu; // [idx] - Hit PolygonNetwork face index
    uint32_t            PrimitiveIndex    = 0xFFFFFFFFu; // [idx] - Hit RayTrianglePrimitive index
    uint16_t            MaterialOrdinal   = 0u;      // [idx] - Hit material / part ordinal
    bool                HitConfirmed      = false;   // [-]   - True iff a valid intersection in [t_min, t_max] occurred
    uint32_t            VisitedWideEntries = 0u;     // [cnt] - Diagnostic BVH4 entries visited during query
    uint32_t            TestedPrimitives   = 0u;     // [cnt] - Diagnostic ray-triangle tests executed
};

struct TriangleSmoothNormals
{
    Mathematics::Axis3D Normal0 { 0.0f, 0.0f, 1.0f }; // [-] - Precomputed smooth vertex normal at Corner0
    Mathematics::Axis3D Normal1 { 0.0f, 0.0f, 1.0f }; // [-] - Precomputed smooth vertex normal at Corner1
    Mathematics::Axis3D Normal2 { 0.0f, 0.0f, 1.0f }; // [-] - Precomputed smooth vertex normal at Corner2
};

class WideBoundingTree
{
public:
    std::vector<WideTreeEntry>         WideEntries      {}; // [-] - Contiguous 128 B 4-wide internal entries (root = 0)
    std::vector<RayTrianglePrimitive>  Primitives       {}; // [-] - Reordered contiguous leaf triangle stream
    std::vector<TriangleSmoothNormals> PrimitiveNormals {}; // [-] - Optional precomputed smooth normals aligned 1:1 with Primitives
    Mathematics::Axis3D                RootBoundsMin    {}; // [m] - Scene AABB minimum
    Mathematics::Axis3D                RootBoundsMax    {}; // [m] - Scene AABB maximum
    uint32_t                           MaxTreeDepth     = 0u; // [cnt] - Maximum 4-wide tree depth

    void Reset() noexcept;

    bool BuildFromPolygonNetwork(const GeometrySpace::PolygonNetwork& Network);
    bool BuildFromTrianglePrimitives(std::span<const RayTrianglePrimitive> InputPrimitives);
    bool BuildFromTrianglePrimitivesWithNormals(
        std::span<const RayTrianglePrimitive>  InputPrimitives,
        std::span<const TriangleSmoothNormals> InputNormals);

    // Deterministic front-to-back ordered 4-wide closest-hit query.
    [[nodiscard]] RayIntersectionResult TraceClosestRay(
        const RaySegmentQuery& Ray,
        bool                   CullBackfaces = false) const noexcept;

    // Early-exit shadow / ambient-occlusion visibility blocker query.
    [[nodiscard]] bool QueryRayOccluded(
        const RaySegmentQuery& Ray,
        uint32_t*              OutVisitedEntries = nullptr) const noexcept;

    [[nodiscard]] std::vector<std::byte> SerializeToBinaryBytes() const;
    [[nodiscard]] bool                   DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes);
};

} // namespace Frontier::SpatialAcceleration
