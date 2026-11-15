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
        case CollisionShapeCategory::ConvexHull:
        {
            if (Shape.ConvexVertices.empty())
            {
                return 0.001f;
            }
            Mathematics::Axis3D MinCorner = Shape.ConvexVertices.front();
            Mathematics::Axis3D MaxCorner = Shape.ConvexVertices.front();
            for (const Mathematics::Axis3D& Vertex : Shape.ConvexVertices)
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

} // namespace Frontier::SimulationSpace::PhysicsSpace
