/*====================================================================================================================================
                                                    SKETCHERREFERENCESURFACE.H
====================================================================================================================================*/
// 🧩 High-poly GPU reference surface container and BVH acceleration for Blender vehicle retopology in SolidArc

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 32-byte GPU vertex matching SketcherReferenceSurface.slang (uploaded once to Vulkan device buffer).
struct SketcherReferenceCorner
{
    float    Position[3] = { 0.0f, 0.0f, 0.0f };                                             // [len] - World position
    float    Normal[3]   = { 0.0f, 0.0f, 1.0f };                                             // [-]   - Unit shading normal
    uint32_t PartIndex   = 0u;                                                               // [-]   - Owning part slot
    uint32_t CreaseBits  = 0u;                                                               // [-]   - Bit 0: sharp/crease edge corner
};
static_assert(sizeof(SketcherReferenceCorner) == 32u, "SketcherReferenceCorner must stay 32 bytes for GPU stride");

// 📝 28-byte GPU feature/crease line segment (matches SketcherSceneSegment instance layout).
struct SketcherReferenceSegment
{
    float    Start[3]  = { 0.0f, 0.0f, 0.0f };                                               // [len] - Segment start
    float    End[3]    = { 0.0f, 0.0f, 0.0f };                                               // [len] - Segment end
    uint32_t PartIndex = 0u;                                                                 // [-]   - Owning part slot
};
static_assert(sizeof(SketcherReferenceSegment) == 28u, "SketcherReferenceSegment must stay 28 bytes for GPU stride");

struct SketcherReferencePart
{
    std::string             Name             = {};                                           // [-]   - Imported Blender object name
    std::string             Collection       = {};                                           // [-]   - Source Blender collection
    std::string             ModifierSummary  = {};                                           // [-]   - Evaluated modifiers (e.g. "Mirror · Subdiv L2")
    uint32_t                FirstCorner      = 0u;                                           // [-]   - Start corner in Corners
    uint32_t                CornerCount      = 0u;                                           // [-]   - Multiple of 3 (triangle soup)
    uint32_t                FirstSegment     = 0u;                                           // [-]   - Start segment in CreaseSegments
    uint32_t                SegmentCount     = 0u;                                           // [-]   - Crease / feature segment count
    std::array<float, 3>    SurfaceColour       = { 0.68f, 0.72f, 0.76f };                      // [0-1] - Linear-ish studio clay/paint tint
    std::array<double, 3>   MinBound         = { 0.0, 0.0, 0.0 };                            // [len] - World AABB min
    std::array<double, 3>   MaxBound         = { 0.0, 0.0, 0.0 };                            // [len] - World AABB max
    bool                    VisibleCondition = true;                                         // [-]   - Drawn & snappable
    bool                    LockedCondition  = false;                                        // [-]   - Frozen in outliner
};

struct SketcherReferenceHit
{
    GeometrySpace::Parametric::Vector3 Position      = {};                                   // [len] - Exact hit or closest point
    GeometrySpace::Parametric::Vector3 Normal        = { 0.0, 0.0, 1.0 };                    // [-]   - Smooth interpolated unit normal
    double                             Distance      = 0.0;                                  // [len] - Ray distance or Euclidean gap
    uint32_t                           SurfaceIndex  = 0u;                                   // [-]   - Index into Document.ReferenceSurfaces
    uint32_t                           PartIndex     = 0u;                                   // [-]   - Index into Surface.Parts
    uint32_t                           TriangleIndex = 0u;                                   // [-]   - Triangle index in Surface.Corners / 3
};

struct SketcherReferenceCreaseHit
{
    GeometrySpace::Parametric::Vector3 Position     = {};                                    // [len] - Point on feature crease segment
    GeometrySpace::Parametric::Vector3 Tangent      = {};                                    // [-]   - Unit direction along crease
    GeometrySpace::Parametric::Vector3 Normal       = { 0.0, 0.0, 1.0 };                     // [-]   - Nearby surface normal
    double                             PixelGap     = 1.0e300;                               // [px]  - Screen distance to pointer
    bool                               EndCondition = false;                                 // [-]   - Snapped to crease endpoint
    uint32_t                           SurfaceIndex = 0u;                                    // [-]   - Owning reference surface
    uint32_t                           PartIndex    = 0u;                                    // [-]   - Owning reference part
};

// 📝 32-byte cache-aligned BVH cell: Count > 0 denotes leaf [LeftOrFirst .. LeftOrFirst + Count);
//    Count == 0 denotes internal cell with children LeftOrFirst and LeftOrFirst + 1.
struct SketcherReferenceBvhCell
{
    float    MinX        = 0.0f;
    float    MinY        = 0.0f;
    float    MinZ        = 0.0f;
    uint32_t LeftOrFirst = 0u;
    float    MaxX        = 0.0f;
    float    MaxY        = 0.0f;
    float    MaxZ        = 0.0f;
    uint32_t Count       = 0u;
};
static_assert(sizeof(SketcherReferenceBvhCell) == 32u, "SketcherReferenceBvhCell must stay 32 bytes");

struct SketcherReferenceSurfaceBvh
{
    std::vector<SketcherReferenceBvhCell> Cells           = {};                              // [-] - Flat BVH hierarchy
    std::vector<uint32_t>                 TriangleIndices = {};                              // [-] - Permuted triangle indices
};

// 📝 Immutable geometry payload shared across document clones (O(1) preview staging even at 1M+ triangles).
struct SketcherReferenceGeometry
{
    std::vector<SketcherReferenceCorner>  Corners        = {};                               // [-] - Triangle vertex soup
    std::vector<SketcherReferenceSegment> CreaseSegments = {};                               // [-] - Feature/crease line segments
    SketcherReferenceSurfaceBvh           Bvh            = {};                               // [-] - Ray + closest-point BVH
};

struct SketcherReferenceSurface
{
    std::string                                Name               = {};                      // [-]   - Display caption (e.g. "Egoist")
    std::string                                SourcePath         = {};                      // [-]   - Imported .blend file path
    std::string                                SummaryNote        = {};                      // [-]   - Stats note (parts, tris, modifiers)
    std::vector<SketcherReferencePart>         Parts              = {};                      // [-]   - Evaluated Blender mesh objects
    std::shared_ptr<const SketcherReferenceGeometry> Geometry     = {};                      // [-]   - Shared GPU vertices + BVH
    std::array<double, 3>                      MinBound           = { 0.0, 0.0, 0.0 };       // [len] - Aggregate world AABB min
    std::array<double, 3>                      MaxBound           = { 0.0, 0.0, 0.0 };       // [len] - Aggregate world AABB max
    float                                      Opacity            = 0.88f;                   // [0-1] - Surface opacity (ghost < 0.98)
    uint32_t                                   MatcapLayer        = 0u;                      // [-]   - 0 studio shading, >0 matcap layer
    bool                                       VisibleCondition   = true;                    // [-]   - Master visibility
    bool                                       LockedCondition    = false;                   // [-]   - Master lock
    bool                                       WireframeCondition = true;                    // [-]   - GPU crease/feature line overlay
    double                                     ScaleFactor        = 1.0;                     // [-]   - Applied unit multiplier

    [[nodiscard]] uint32_t TriangleCount() const noexcept
    {
        return Geometry ? static_cast<uint32_t>(Geometry->Corners.size() / 3u) : 0u;
    }

    [[nodiscard]] uint32_t SegmentCount() const noexcept
    {
        return Geometry ? static_cast<uint32_t>(Geometry->CreaseSegments.size()) : 0u;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build an O(N log N) BVH over all triangles of Corners (each 3 consecutive entries = 1 triangle).
[[nodiscard]] SketcherReferenceSurfaceBvh BuildReferenceSurfaceBvh(const std::vector<SketcherReferenceCorner>& Corners);

// Exact O(log N) Möller-Trumbore ray hit against all visible parts of Surface (with optional section plane a*x+b*y+c*z+d >= 0).
[[nodiscard]] std::optional<SketcherReferenceHit> IntersectReferenceSurface(
    const SketcherReferenceSurface&            Surface,
    uint32_t                                   SurfaceIndex,
    const GeometrySpace::Parametric::Vector3&  RayOrigin,
    const GeometrySpace::Parametric::Vector3&  RayDirection,
    double                                     MaxDistance  = 1.0e30,
    const std::array<double, 4>*               SectionPlane = nullptr);

// Exact O(log N) ray hit across an entire list of reference surfaces, returning the closest visible hit.
[[nodiscard]] std::optional<SketcherReferenceHit> IntersectReferenceSurfaces(
    const std::vector<SketcherReferenceSurface>& Surfaces,
    const GeometrySpace::Parametric::Vector3&    RayOrigin,
    const GeometrySpace::Parametric::Vector3&    RayDirection,
    double                                       MaxDistance  = 1.0e30,
    const std::array<double, 4>*                 SectionPlane = nullptr);

// Exact O(log N) closest-point projection onto all visible parts of Surface (for precision curve retopology & Shrinkwrap).
[[nodiscard]] std::optional<SketcherReferenceHit> ProjectOntoReferenceSurface(
    const SketcherReferenceSurface&            Surface,
    uint32_t                                   SurfaceIndex,
    const GeometrySpace::Parametric::Vector3&  QueryPoint,
    double                                     MaxRadius = 1.0e30);

// Exact O(log N) closest-point projection across all visible reference surfaces.
[[nodiscard]] std::optional<SketcherReferenceHit> ProjectOntoReferenceSurfaces(
    const std::vector<SketcherReferenceSurface>& Surfaces,
    const GeometrySpace::Parametric::Vector3&    QueryPoint,
    double                                       MaxRadius = 1.0e30);

// Rescale all vertices, crease segments, bounds, and rebuild BVH by RelativeFactor (used by Scale controls).
void RescaleReferenceSurface(SketcherReferenceSurface& Surface, double RelativeFactor);

} // namespace Frontier::Workspaces::Sketcher
