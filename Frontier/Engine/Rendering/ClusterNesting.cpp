/*==========================================================================================================================================
                                                     CLUSTER NESTING DEFINITION
==========================================================================================================================================*/

#include "ClusterNesting.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{

SpatialVector Add(const SpatialVector& A, const SpatialVector& B) noexcept
{
    return {A.X + B.X, A.Y + B.Y, A.Z + B.Z};
}

SpatialVector Subtract(const SpatialVector& A, const SpatialVector& B) noexcept
{
    return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
}

SpatialVector Scale(const SpatialVector& A, double S) noexcept
{
    return {A.X * S, A.Y * S, A.Z * S};
}

double Length(const SpatialVector& A) noexcept
{
    return std::sqrt(A.X * A.X + A.Y * A.Y + A.Z * A.Z);
}

SpatialVector TriangleCentroid(const TriangleMesh& Mesh, uint32_t Triangle) noexcept
{
    const SpatialVector& A = Mesh.Vertices[Mesh.Indices[Triangle * 3 + 0]];
    const SpatialVector& B = Mesh.Vertices[Mesh.Indices[Triangle * 3 + 1]];
    const SpatialVector& C = Mesh.Vertices[Mesh.Indices[Triangle * 3 + 2]];
    return Scale(Add(Add(A, B), C), 1.0 / 3.0);
}

double AxisValue(const SpatialVector& V, int Axis) noexcept
{
    return Axis == 0 ? V.X : (Axis == 1 ? V.Y : V.Z);
}

// Enclosing sphere of a set of child spheres: triangle-weighted centre, radius grown to enclose each child.
BoundingSphere Enclose(const ClusterNode* Children, uint32_t Count) noexcept
{
    BoundingSphere Result;
    double         WeightTotal = 0.0;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const double Weight = static_cast<double>(std::max(Children[Index].TriangleCount, 1u));
        Result.Centre       = Add(Result.Centre, Scale(Children[Index].Bounds.Centre, Weight));
        WeightTotal += Weight;
    }
    if (WeightTotal > 0.0)
    {
        Result.Centre = Scale(Result.Centre, 1.0 / WeightTotal);
    }
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const double Reach =
            Length(Subtract(Children[Index].Bounds.Centre, Result.Centre)) + Children[Index].Bounds.Radius;
        Result.Radius = std::max(Result.Radius, Reach);
    }
    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          BUILD
//------------------------------------------------------------------------------------------------------------------------

bool ClusterNesting::Build(const TriangleMesh& Mesh, uint32_t MaxClusterTriangles)
{
    Nodes_.clear();
    ChildIndices_.clear();
    TriangleOrder_.clear();
    TriangleTotal_ = 0;
    RootIndex_     = 0;

    if (MaxClusterTriangles == 0 || Mesh.Indices.size() < 3)
    {
        return false;
    }
    const uint32_t Triangles = static_cast<uint32_t>(Mesh.Indices.size() / 3);
    for (const uint32_t Index : Mesh.Indices)
    {
        if (Index >= Mesh.Vertices.size())
        {
            return false;   // Refused: a dangling index would poison every bound above it
        }
    }

    std::vector<uint32_t> Working(Triangles);
    for (uint32_t Triangle = 0; Triangle < Triangles; ++Triangle)
    {
        Working[Triangle] = Triangle;
    }
    RootIndex_     = BuildGroupRecursive(Mesh, Working, 0, Triangles, MaxClusterTriangles, 0);
    TriangleOrder_ = Working;
    TriangleTotal_ = Triangles;
    return true;
}

uint32_t ClusterNesting::BuildGroupRecursive(const TriangleMesh& Mesh, std::vector<uint32_t>& Working,
                                             uint32_t First, uint32_t Count, uint32_t MaxClusterTriangles,
                                             uint32_t DepthTier)
{
    if (Count <= MaxClusterTriangles)
    {
        // Leaf: bound every vertex of every triangle in the group.
        ClusterNode Node;
        Node.FirstTriangle = First;
        Node.TriangleCount = Count;
        Node.DepthTier     = DepthTier;
        SpatialVector Centre{};
        for (uint32_t Slot = 0; Slot < Count; ++Slot)
        {
            Centre = Add(Centre, TriangleCentroid(Mesh, Working[First + Slot]));
        }
        Node.Bounds.Centre = Scale(Centre, 1.0 / static_cast<double>(Count));
        double Radius      = 0.0;
        for (uint32_t Slot = 0; Slot < Count; ++Slot)
        {
            const uint32_t Triangle = Working[First + Slot];
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                Radius = std::max(Radius, Length(Subtract(Mesh.Vertices[Mesh.Indices[Triangle * 3 + Corner]],
                                                          Node.Bounds.Centre)));
            }
        }
        Node.Bounds.Radius  = Radius;
        Node.GeometricError = 0.0;
        const uint32_t Index = static_cast<uint32_t>(Nodes_.size());
        Nodes_.push_back(Node);
        return Index;
    }

    // Split at the median along the longest centroid axis (deterministic for a given mesh).
    SpatialVector Minimum{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::max()};
    SpatialVector Maximum{-Minimum.X, -Minimum.Y, -Minimum.Z};
    std::vector<SpatialVector> Centroids(Count);
    for (uint32_t Slot = 0; Slot < Count; ++Slot)
    {
        Centroids[Slot]  = TriangleCentroid(Mesh, Working[First + Slot]);
        Minimum.X        = std::min(Minimum.X, Centroids[Slot].X);
        Minimum.Y        = std::min(Minimum.Y, Centroids[Slot].Y);
        Minimum.Z        = std::min(Minimum.Z, Centroids[Slot].Z);
        Maximum.X        = std::max(Maximum.X, Centroids[Slot].X);
        Maximum.Y        = std::max(Maximum.Y, Centroids[Slot].Y);
        Maximum.Z        = std::max(Maximum.Z, Centroids[Slot].Z);
    }
    const SpatialVector Extent = Subtract(Maximum, Minimum);
    const int Axis = (Extent.X >= Extent.Y && Extent.X >= Extent.Z) ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);

    const uint32_t         Half = Count / 2;
    std::vector<uint32_t>* Order = &Working;
    std::nth_element(Working.begin() + First, Working.begin() + First + Half, Working.begin() + First + Count,
                     [&Mesh, Axis](uint32_t A, uint32_t B) {
                         return AxisValue(TriangleCentroid(Mesh, A), Axis) <
                                AxisValue(TriangleCentroid(Mesh, B), Axis);
                     });
    (void)Order;

    const uint32_t LeftChild =
        BuildGroupRecursive(Mesh, Working, First, Half, MaxClusterTriangles, DepthTier + 1);
    const uint32_t RightChild =
        BuildGroupRecursive(Mesh, Working, First + Half, Count - Half, MaxClusterTriangles, DepthTier + 1);

    // Node indices are post-order, so children are recorded in their own table, grouped per parent.
    ClusterNode Node;
    Node.FirstChild    = static_cast<uint32_t>(ChildIndices_.size());
    Node.ChildCount    = 2;
    ChildIndices_.push_back(LeftChild);
    ChildIndices_.push_back(RightChild);
    Node.DepthTier     = DepthTier;
    Node.TriangleCount = Count;

    const ClusterNode Children[2] = {Nodes_[LeftChild], Nodes_[RightChild]};
    Node.Bounds                   = Enclose(Children, 2);

    // Monotone nesting error (sphere-tree form): the coarsest child error plus the radial slack this
    // level adds over its largest child. Zero at leaves, non-decreasing to the root — the portable
    // stand-in for simplification error until the DAG phase replaces it with measured deviation.
    const double MaxChildError  = std::max(Children[0].GeometricError, Children[1].GeometricError);
    const double MaxChildRadius = std::max(Children[0].Bounds.Radius, Children[1].Bounds.Radius);
    Node.GeometricError = MaxChildError + std::max(Node.Bounds.Radius - MaxChildRadius, 0.0);

    const uint32_t Index = static_cast<uint32_t>(Nodes_.size());
    Nodes_.push_back(Node);
    return Index;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CUT SELECTION
//------------------------------------------------------------------------------------------------------------------------

double ClusterNesting::ProjectionFactor(double ViewportHeightPx, double FovYRadians) noexcept
{
    const double Tangent = std::tan(FovYRadians * 0.5);
    return Tangent > 0.0 ? (ViewportHeightPx * 0.5) / Tangent : 0.0;
}

double ClusterNesting::ProjectedErrorPx(const ClusterNode& Node, const SpatialVector& CameraPosition,
                                        double ProjectionFactorPx) const noexcept
{
    if (Node.GeometricError <= 0.0)
    {
        return 0.0;
    }
    const double Distance = Length(Subtract(Node.Bounds.Centre, CameraPosition));
    const double NearSide = std::max(Distance - Node.Bounds.Radius, 1e-6);
    return Node.GeometricError * ProjectionFactorPx / NearSide;
}

void ClusterNesting::SelectCut(const SpatialVector& CameraPosition, double ProjectionFactorPx,
                               double ErrorThresholdPx, std::vector<uint32_t>& OutNodeIndices) const
{
    OutNodeIndices.clear();
    if (Nodes_.empty())
    {
        return;
    }
    std::vector<uint32_t> Stack{RootIndex_};
    while (!Stack.empty())
    {
        const uint32_t    Index = Stack.back();
        Stack.pop_back();
        const ClusterNode& Node = Nodes_[Index];
        if (Node.ChildCount == 0 ||
            ProjectedErrorPx(Node, CameraPosition, ProjectionFactorPx) <= ErrorThresholdPx)
        {
            OutNodeIndices.push_back(Index);
            continue;
        }
        for (uint32_t Slot = Node.FirstChild + Node.ChildCount; Slot-- > Node.FirstChild;)
        {
            Stack.push_back(ChildIndices_[Slot]);
        }
    }
}

uint64_t ClusterNesting::StructureHash() const noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    const auto Mix = [&Hash](uint64_t Value) {
        for (int Byte = 0; Byte < 8; ++Byte)
        {
            Hash ^= (Value >> (Byte * 8)) & 0xFFull;
            Hash *= 1099511628211ull;
        }
    };
    Mix(Nodes_.size());
    Mix(TriangleTotal_);
    for (const ClusterNode& Node : Nodes_)
    {
        Mix(static_cast<uint64_t>(std::llround(Node.Bounds.Centre.X * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(Node.Bounds.Centre.Y * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(Node.Bounds.Centre.Z * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(Node.Bounds.Radius * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(Node.GeometricError * 1e6)));
        Mix(Node.TriangleCount);
        Mix(Node.ChildCount);
    }
    return Hash;
}

} // namespace Frontier
