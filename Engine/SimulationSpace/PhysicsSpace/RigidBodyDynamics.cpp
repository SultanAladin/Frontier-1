/*============================================================================================================================================
                                                          RIGIDBODYDYNAMICS.CPP
============================================================================================================================================*/
// 🧩 Analytic volume, effective mass, principal inertia, and quaternion rotation utilities for rigid bodies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

float RigidBodyDynamics::EstimateShapeVolumeM3(const RigidShapeDescriptor& Shape) noexcept
{
    switch (Shape.ShapeCategory)
    {
        case CollisionShapeCategory::Box:
        {
            const float Width  = std::max(Shape.HalfExtentsMeters.X * 2.0f, 1.0e-4f);
            const float Depth  = std::max(Shape.HalfExtentsMeters.Y * 2.0f, 1.0e-4f);
            const float Height = std::max(Shape.HalfExtentsMeters.Z * 2.0f, 1.0e-4f);
            return Width * Depth * Height;
        }
        case CollisionShapeCategory::Sphere:
        {
            const float Radius = std::max(Shape.RadiusMeters, 1.0e-4f);
            return (4.0f / 3.0f) * static_cast<float>(Constants::Pi) * Radius * Radius * Radius;
        }
        case CollisionShapeCategory::Capsule:
        {
            const float Radius   = std::max(Shape.RadiusMeters, 1.0e-4f);
            const float Cylinder = static_cast<float>(Constants::Pi) * Radius * Radius * std::max(Shape.HalfHeightMeters * 2.0f, 1.0e-4f);
            const float Caps     = (4.0f / 3.0f) * static_cast<float>(Constants::Pi) * Radius * Radius * Radius;
            return Cylinder + Caps;
        }
        case CollisionShapeCategory::Cylinder:
        {
            const float Radius = std::max(Shape.RadiusMeters, 1.0e-4f);
            return static_cast<float>(Constants::Pi) * Radius * Radius * std::max(Shape.HalfHeightMeters * 2.0f, 1.0e-4f);
        }
        case CollisionShapeCategory::ConvexHull:
        case CollisionShapeCategory::CompoundHull:
        case CollisionShapeCategory::TriangleMesh:
        {
            const auto& Verts = !Shape.ConvexVertices.empty() ? Shape.ConvexVertices : Shape.MeshTriangles;
            if (Verts.empty())
            {
                return 0.001f;
            }
            Mathematics::Axis3D MinCorner = Verts.front();
            Mathematics::Axis3D MaxCorner = Verts.front();
            for (const Mathematics::Axis3D& Vertex : Verts)
            {
                MinCorner.X = std::min(MinCorner.X, Vertex.X);
                MinCorner.Y = std::min(MinCorner.Y, Vertex.Y);
                MinCorner.Z = std::min(MinCorner.Z, Vertex.Z);
                MaxCorner.X = std::max(MaxCorner.X, Vertex.X);
                MaxCorner.Y = std::max(MaxCorner.Y, Vertex.Y);
                MaxCorner.Z = std::max(MaxCorner.Z, Vertex.Z);
            }
            const float BoxVol = std::max(MaxCorner.X - MinCorner.X, 1.0e-3f)
                               * std::max(MaxCorner.Y - MinCorner.Y, 1.0e-3f)
                               * std::max(MaxCorner.Z - MinCorner.Z, 1.0e-3f);
            return BoxVol * 0.55f;
        }
    }
    return 1.0f;
}

float RigidBodyDynamics::ResolveEffectiveMassKg(const RigidBodyDescriptor& Descriptor) noexcept
{
    if (Descriptor.MotionCategory != RigidMotionCategory::Dynamic)
    {
        return 0.0f;
    }
    if (Descriptor.ExplicitMassKg > 0.0f)
    {
        return Descriptor.ExplicitMassKg;
    }
    const float Density = std::max(Descriptor.Shape.DensityKgPerM3, 1.0f);
    return std::max(EstimateShapeVolumeM3(Descriptor.Shape) * Density, 0.01f);
}

Mathematics::Scalar3D RigidBodyDynamics::EstimatePrincipalInertiaKgM2(
    const RigidShapeDescriptor& Shape,
    float                       MassKg) noexcept
{
    if (MassKg <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    if (Shape.ShapeCategory == CollisionShapeCategory::Sphere)
    {
        const float I = 0.4f * MassKg * Shape.RadiusMeters * Shape.RadiusMeters;
        return { I, I, I };
    }

    const float W = std::max(Shape.HalfExtentsMeters.X * 2.0f, 1.0e-3f);
    const float D = std::max(Shape.HalfExtentsMeters.Y * 2.0f, 1.0e-3f);
    const float H = std::max(Shape.HalfExtentsMeters.Z * 2.0f, 1.0e-3f);
    const float Factor = MassKg / 12.0f;
    return {
        Factor * (D * D + H * H),
        Factor * (W * W + H * H),
        Factor * (W * W + D * D),
    };
}

RigidOrientationQuaternion RigidBodyDynamics::NormalizeQuaternion(RigidOrientationQuaternion Quat) noexcept
{
    const float LengthSq = Quat.X * Quat.X + Quat.Y * Quat.Y + Quat.Z * Quat.Z + Quat.W * Quat.W;
    if (LengthSq < 1.0e-12f)
    {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    const float InvLen = 1.0f / std::sqrt(LengthSq);
    return { Quat.X * InvLen, Quat.Y * InvLen, Quat.Z * InvLen, Quat.W * InvLen };
}

Mathematics::Axis3D RigidBodyDynamics::RotateByQuaternion(
    RigidOrientationQuaternion Quat,
    Mathematics::Axis3D        Vector) noexcept
{
    const float Tx = 2.0f * (Quat.Y * Vector.Z - Quat.Z * Vector.Y);
    const float Ty = 2.0f * (Quat.Z * Vector.X - Quat.X * Vector.Z);
    const float Tz = 2.0f * (Quat.X * Vector.Y - Quat.Y * Vector.X);

    return {
        Vector.X + Quat.W * Tx + (Quat.Y * Tz - Quat.Z * Ty),
        Vector.Y + Quat.W * Ty + (Quat.Z * Tx - Quat.X * Tz),
        Vector.Z + Quat.W * Tz + (Quat.X * Ty - Quat.Y * Tx),
    };
}

Mathematics::Axis3D RigidBodyDynamics::RotateByConjugateQuaternion(
    RigidOrientationQuaternion Quat,
    Mathematics::Axis3D        Vector) noexcept
{
    return RotateByQuaternion({ -Quat.X, -Quat.Y, -Quat.Z, Quat.W }, Vector);
}

BakedColliderGeometry RigidBodyDynamics::BakeConvexHullFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             MaxHullVertices,
    float                                MinYFloorClip)
{
    BakedColliderGeometry Out {};
    Out.ShapeCategory = CollisionShapeCategory::ConvexHull;
    if (Network.Vertices.empty())
    {
        return Out;
    }

    Mathematics::Axis3D MinB {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxB { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : Network.Vertices)
    {
        if (V.Axes.Y < MinYFloorClip)
        {
            continue;
        }
        MinB.X = std::min(MinB.X, V.Axes.X); MaxB.X = std::max(MaxB.X, V.Axes.X);
        MinB.Y = std::min(MinB.Y, V.Axes.Y); MaxB.Y = std::max(MaxB.Y, V.Axes.Y);
        MinB.Z = std::min(MinB.Z, V.Axes.Z); MaxB.Z = std::max(MaxB.Z, V.Axes.Z);
    }
    if (MinB.X > MaxB.X)
    {
        return Out;
    }
    Out.BoundsMin = MinB;
    Out.BoundsMax = MaxB;

    const uint32_t Budget = std::clamp(MaxHullVertices, 16u, 256u);
    const std::size_t Stride = std::max<std::size_t>(1u, Network.Vertices.size() / Budget);
    Out.ConvexVertices.reserve(Budget + 8u);

    // Always include extreme vertices along all 6 principal directions + 8 diagonal octants for tight hull fidelity
    constexpr std::array<Mathematics::Axis3D, 14u> ProbeDirs {{
        {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        {  1.0f,  1.0f,  1.0f }, { -1.0f,  1.0f,  1.0f },
        {  1.0f, -1.0f,  1.0f }, { -1.0f, -1.0f,  1.0f },
        {  1.0f,  1.0f, -1.0f }, { -1.0f,  1.0f, -1.0f },
        {  1.0f, -1.0f, -1.0f }, { -1.0f, -1.0f, -1.0f },
    }};

    for (const Mathematics::Axis3D& Dir : ProbeDirs)
    {
        float BestDot = -1.0e30f;
        Mathematics::Axis3D BestPt = MinB;
        for (const auto& V : Network.Vertices)
        {
            if (V.Axes.Y < MinYFloorClip)
            {
                continue;
            }
            const float D = V.Axes.X * Dir.X + V.Axes.Y * Dir.Y + V.Axes.Z * Dir.Z;
            if (D > BestDot)
            {
                BestDot = D;
                BestPt  = V.Axes;
            }
        }
        Out.ConvexVertices.push_back(BestPt);
    }

    for (std::size_t I = 0u; I < Network.Vertices.size() && Out.ConvexVertices.size() < Budget; I += Stride)
    {
        const Mathematics::Axis3D P = Network.Vertices[I].Axes;
        if (P.Y >= MinYFloorClip)
        {
            Out.ConvexVertices.push_back(P);
        }
    }

    return Out;
}

BakedColliderGeometry RigidBodyDynamics::BakeCompoundHullFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             SliceCount,
    float                                MinYFloorClip)
{
    BakedColliderGeometry Out {};
    Out.ShapeCategory = CollisionShapeCategory::CompoundHull;
    if (Network.Vertices.empty())
    {
        return Out;
    }

    Mathematics::Axis3D MinB {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxB { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : Network.Vertices)
    {
        if (V.Axes.Y < MinYFloorClip)
        {
            continue;
        }
        MinB.X = std::min(MinB.X, V.Axes.X); MaxB.X = std::max(MaxB.X, V.Axes.X);
        MinB.Y = std::min(MinB.Y, V.Axes.Y); MaxB.Y = std::max(MaxB.Y, V.Axes.Y);
        MinB.Z = std::min(MinB.Z, V.Axes.Z); MaxB.Z = std::max(MaxB.Z, V.Axes.Z);
    }
    if (MinB.X >= MaxB.X)
    {
        return Out;
    }
    Out.BoundsMin = MinB;
    Out.BoundsMax = MaxB;

    // Partition along longitudinal +X into convex sub-hull slices (e.g., rear diffuser/tail, rear cabin, greenhouse, front nose)
    const uint32_t NumSlices = std::clamp(SliceCount, 2u, 8u);
    const float SpanX        = std::max(MaxB.X - MinB.X, 1.0e-3f);
    const float SliceWidth   = SpanX / static_cast<float>(NumSlices);

    for (uint32_t S = 0u; S < NumSlices; ++S)
    {
        const float X0 = MinB.X + static_cast<float>(S) * SliceWidth - 0.02f;
        const float X1 = MinB.X + static_cast<float>(S + 1u) * SliceWidth + 0.02f;

        std::vector<Mathematics::Axis3D> SlicePts {};
        Mathematics::Axis3D SMin {  1.0e9f,  1.0e9f,  1.0e9f };
        Mathematics::Axis3D SMax { -1.0e9f, -1.0e9f, -1.0e9f };
        for (const auto& V : Network.Vertices)
        {
            const Mathematics::Axis3D P = V.Axes;
            if (P.Y < MinYFloorClip || P.X < X0 || P.X > X1)
            {
                continue;
            }
            SMin.X = std::min(SMin.X, P.X); SMax.X = std::max(SMax.X, P.X);
            SMin.Y = std::min(SMin.Y, P.Y); SMax.Y = std::max(SMax.Y, P.Y);
            SMin.Z = std::min(SMin.Z, P.Z); SMax.Z = std::max(SMax.Z, P.Z);
            SlicePts.push_back(P);
        }

        if (SlicePts.size() < 4u || (SMax.X - SMin.X) < 0.02f || (SMax.Y - SMin.Y) < 0.02f || (SMax.Z - SMin.Z) < 0.02f)
        {
            continue;
        }

        // Subsample up to 24 hull points per slice while keeping slice extrema
        const uint32_t StartCount = static_cast<uint32_t>(Out.ConvexVertices.size());
        const std::size_t Stride  = std::max<std::size_t>(1u, SlicePts.size() / 24u);
        for (std::size_t I = 0u; I < SlicePts.size() && (Out.ConvexVertices.size() - StartCount) < 24u; I += Stride)
        {
            Out.ConvexVertices.push_back(SlicePts[I]);
        }
        // Guarantee non-degenerate 3D span in each slice
        Out.ConvexVertices.push_back({ SMin.X, SMin.Y, SMin.Z });
        Out.ConvexVertices.push_back({ SMax.X, SMax.Y, SMax.Z });
        const uint32_t Added = static_cast<uint32_t>(Out.ConvexVertices.size()) - StartCount;
        if (Added >= 4u)
        {
            Out.CompoundHullVertexCounts.push_back(Added);
        }
    }

    return Out;
}

BakedColliderGeometry RigidBodyDynamics::BakeTriangleMeshFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network)
{
    BakedColliderGeometry Out {};
    Out.ShapeCategory = CollisionShapeCategory::TriangleMesh;
    if (Network.Faces.empty() || Network.Vertices.empty())
    {
        return Out;
    }

    Mathematics::Axis3D MinB {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxB { -1.0e9f, -1.0e9f, -1.0e9f };
    Out.MeshTriangles.reserve(Network.Faces.size() * 3u);

    for (const GeometrySpace::Face& F : Network.Faces)
    {
        if (F.CornerCount < 3u || F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }

        std::vector<Mathematics::Axis3D> Poly {};
        Poly.reserve(F.CornerCount);
        uint32_t EdgeCursor = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount; ++Step)
        {
            const Mathematics::Axis3D P = Network.Vertices[Network.Edges[EdgeCursor].OriginVertexIndex].Axes;
            MinB.X = std::min(MinB.X, P.X); MaxB.X = std::max(MaxB.X, P.X);
            MinB.Y = std::min(MinB.Y, P.Y); MaxB.Y = std::max(MaxB.Y, P.Y);
            MinB.Z = std::min(MinB.Z, P.Z); MaxB.Z = std::max(MaxB.Z, P.Z);
            Poly.push_back(P);
            EdgeCursor = Network.Edges[EdgeCursor].NextEdgeIndex;
        }

        // Fan-triangulate n-gon
        for (std::size_t Tri = 1u; Tri + 1u < Poly.size(); ++Tri)
        {
            Out.MeshTriangles.push_back(Poly[0u]);
            Out.MeshTriangles.push_back(Poly[Tri]);
            Out.MeshTriangles.push_back(Poly[Tri + 1u]);
        }
    }

    Out.BoundsMin = MinB;
    Out.BoundsMax = MaxB;
    return Out;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
