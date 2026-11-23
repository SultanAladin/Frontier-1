/*==========================================================================================================================================
                                                     CLUSTER NESTING DECLARATION
                                     ------------------------------------------------------------------
                 Triangles grouped into bounded clusters, clusters nested into a hierarchy with monotone
                      geometric error, and a screen-space-error cut chosen per camera — the portable
                          core of the geometry pipeline (plan section 7 row 3; decision D5).
==========================================================================================================================================*/

// ClusterNesting: the portable (CPU) hierarchy the GPU phase mirrors. It builds a binary nesting of
// triangle clusters over an indexed mesh, computes an enclosing sphere and a monotone geometric error
// per node, and selects per-frame "cuts": the coarsest set of nodes whose projected error stays within
// a pixel budget. Nothing here samples the demo corpus — validation generates procedural meshes and
// proves the invariants (exactly-once coverage, monotone error, monotone refinement) headlessly.
// The full simplification DAG of the demo's ClusterNesting lands with the Slang/GPU phase and consumes
// this same shape.

#pragma once

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       GEOMETRY TYPES
//------------------------------------------------------------------------------------------------------------------------

// Double precision throughout the portable core so validation is exact and deterministic.
struct SpatialVector
{
    double X = 0.0;   // [m]
    double Y = 0.0;   // [m]
    double Z = 0.0;   // [m]
};

struct BoundingSphere
{
    SpatialVector Centre;      // [m]
    double        Radius = 0.0;   // [m]
};

struct TriangleMesh
{
    std::vector<SpatialVector> Vertices;   // [m]
    std::vector<uint32_t>      Indices;    // Three per triangle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CLUSTER NODES
//------------------------------------------------------------------------------------------------------------------------

struct ClusterNode
{
    BoundingSphere Bounds;                    // Encloses every triangle in the subtree
    double         GeometricError  = 0.0;     // [m] - Monotone non-decreasing towards the root; 0 at leaves
    uint32_t       FirstTriangle   = 0;       // Into ClusterNesting::TriangleOrder (leaves only)
    uint32_t       TriangleCount   = 0;       // Subtree total
    uint32_t       FirstChild      = 0;       // Into ClusterNesting::ChildIndices; ChildCount == 0 = leaf
    uint32_t       ChildCount      = 0;
    uint32_t       DepthTier       = 0;       // 0 at the root
};

class ClusterNesting
{
  public:
    // Builds the nesting over Mesh. Triangles are recursively split along the longest centroid axis at
    // the median until a group holds at most MaxClusterTriangles. Returns false (and stays empty) for a
    // mesh with no whole triangles or an out-of-range index — a refused build, never a partial one.
    bool Build(const TriangleMesh& Mesh, uint32_t MaxClusterTriangles = 128);

    [[nodiscard]] bool     Empty() const noexcept { return Nodes_.empty(); }
    [[nodiscard]] uint32_t TriangleTotal() const noexcept { return TriangleTotal_; }

    [[nodiscard]] const std::vector<ClusterNode>& Nodes() const noexcept { return Nodes_; }
    [[nodiscard]] const std::vector<uint32_t>&    ChildIndices() const noexcept { return ChildIndices_; }
    [[nodiscard]] const std::vector<uint32_t>&    TriangleOrder() const noexcept { return TriangleOrder_; }
    [[nodiscard]] uint32_t                        RootIndex() const noexcept { return RootIndex_; }

    // [px] per [m] at unit distance: (ViewportHeightPx / 2) / tan(FovY / 2).
    [[nodiscard]] static double ProjectionFactor(double ViewportHeightPx, double FovYRadians) noexcept;

    // Projected error of one node from a camera position. Distance is measured to the near side of the
    // bounds so an enclosing camera clamps to the smallest step instead of dividing by zero.
    [[nodiscard]] double ProjectedErrorPx(const ClusterNode& Node, const SpatialVector& CameraPosition,
                                          double ProjectionFactorPx) const noexcept;

    // The per-frame cut: coarsest nodes whose projected error fits ErrorThresholdPx. The cut partitions
    // the mesh — every triangle is covered exactly once. Out receives node indices, coarsest-first (DFS).
    void SelectCut(const SpatialVector& CameraPosition, double ProjectionFactorPx, double ErrorThresholdPx,
                   std::vector<uint32_t>& OutNodeIndices) const;

    // FNV-1a over the node table (bounds + error, quantised) — two builds of the same mesh must hash equal.
    [[nodiscard]] uint64_t StructureHash() const noexcept;

  private:
    struct BuildGroup
    {
        uint32_t First = 0;   // Into the working triangle list
        uint32_t Count = 0;
    };

    uint32_t BuildGroupRecursive(const TriangleMesh& Mesh, std::vector<uint32_t>& Working, uint32_t First,
                                 uint32_t Count, uint32_t MaxClusterTriangles, uint32_t DepthTier);

    std::vector<ClusterNode> Nodes_;
    std::vector<uint32_t>    ChildIndices_;    // Node indices, grouped per parent (FirstChild + ChildCount)
    std::vector<uint32_t>    TriangleOrder_;   // Original triangle indices, leaf-contiguous
    uint32_t                 RootIndex_     = 0;
    uint32_t                 TriangleTotal_ = 0;
};

} // namespace Frontier
