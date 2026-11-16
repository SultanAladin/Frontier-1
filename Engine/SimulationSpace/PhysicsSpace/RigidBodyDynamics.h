/*============================================================================================================================================
                                                           RIGIDBODYDYNAMICS.H
============================================================================================================================================*/
// 🧩 Rigid-body shape descriptors, motion categories, generation-checked tokens, and inertial tensor evaluators (RHR +Z Up)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/PhysicsConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class RigidMotionCategory : uint8_t
{
    Static    = 0u, // [-] - Immovable world collider (zero inverse mass)
    Kinematic = 1u, // [-] - Scripted collider driving dynamic contacts
    Dynamic   = 2u, // [-] - Fully simulated rigid body integrated by JoltPhysics
};

enum class CollisionShapeCategory : uint8_t
{
    Box          = 0u, // [-] - Oriented box defined by HalfExtentsMeters
    Sphere       = 1u, // [-] - Analytical sphere defined by RadiusMeters
    Capsule      = 2u, // [-] - +Z-aligned capsule defined by HalfHeightMeters + RadiusMeters
    ConvexHull   = 3u, // [-] - Convex polyhedron built from ConvexVertices
    CompoundHull = 4u, // [-] - Multi-hull StaticCompound built from CompoundHullVertexCounts slices inside ConvexVertices
    TriangleMesh = 5u, // [-] - Exact static triangle mesh built from MeshTriangles (3 vertices per triangle)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RigidBodyToken
{
    uint32_t SlotIndex  = 0u; // [idx]   - 1-based contiguous slot index (0 = invalid)
    uint32_t Generation = 0u; // [count] - Monotonic generation stamp guarding against stale access

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return SlotIndex != 0u && Generation != 0u;
    }

    [[nodiscard]] constexpr bool operator==(const RigidBodyToken&) const noexcept = default;
};

struct RigidOrientationQuaternion
{
    float X = 0.0f; // [-] - Imaginary i component
    float Y = 0.0f; // [-] - Imaginary j component
    float Z = 0.0f; // [-] - Imaginary k component
    float W = 1.0f; // [-] - Scalar real component

    [[nodiscard]] constexpr bool operator==(const RigidOrientationQuaternion&) const noexcept = default;
};

struct RigidShapeDescriptor
{
    CollisionShapeCategory               ShapeCategory      = CollisionShapeCategory::Box; // [-]      - Analytical or convex collider shape
    Mathematics::Scalar3D                HalfExtentsMeters  { 0.5f, 0.5f, 0.5f };          // [m]      - Half-extents along X, Y, Z for Box
    float                                RadiusMeters       = 0.5f;                        // [m]      - Radius for Sphere or Capsule
    float                                HalfHeightMeters   = 0.5f;                        // [m]      - Cylinder half-height along +Z for Capsule
    float                                ConvexRadiusMeters = 0.01f;                       // [m]      - Convex margin bevel radius
    std::span<const Mathematics::Axis3D> ConvexVertices     {};                            // [m]      - Non-owning hull vertex span for ConvexHull / CompoundHull
    std::span<const uint32_t>            CompoundHullVertexCounts {};                      // [count]  - Vertex counts per sub-hull inside ConvexVertices
    std::span<const Mathematics::Axis3D> MeshTriangles      {};                            // [m]      - Non-owning triangle triplets (3*N) for TriangleMesh
    float                                DensityKgPerM3     = 2350.0f;                     // [kg/m^3] - Material bulk density
    float                                Friction           = 0.65f;                       // [-]      - Coulomb contact friction coefficient
    float                                Restitution        = 0.15f;                       // [0-1]    - Coefficient of restitution
};

struct RigidBodyDescriptor
{
    RigidMotionCategory        MotionCategory    = RigidMotionCategory::Dynamic; // [-]     - Static, Kinematic, or Dynamic integration
    uint16_t                   CollisionChannel  = 1u;                           // [idx]   - Broadphase layer channel ordinal
    Mathematics::Axis3D        PositionMeters    { 0.0f, 0.0f, 0.0f };           // [m]     - World origin position (RHR +Z Up)
    RigidOrientationQuaternion Orientation       {};                             // [-]     - Initial unit orientation quaternion
    Mathematics::Axis3D        LinearVelocity    { 0.0f, 0.0f, 0.0f };           // [m/s]   - Initial linear velocity
    Mathematics::Axis3D        AngularVelocity   { 0.0f, 0.0f, 0.0f };           // [rad/s] - Initial angular velocity
    float                      ExplicitMassKg    = 0.0f;                         // [kg]    - Explicit mass override (>0 overrides Density * Volume)
    float                      LinearDamping     = 0.02f;                        // [1/s]   - Translational velocity damping
    float                      AngularDamping    = 0.05f;                        // [1/s]   - Rotational velocity damping
    float                      GravityFactor     = 1.0f;                         // [-]     - Gravity multiplier
    float                      ForceSensitivity  = 1.0f;                         // [-]     - Multiplier on ForceFields aerodynamic acceleration
    bool                       AllowSleep        = true;                         // [-]     - Permits solver island deactivation when resting
    bool                       ContinuousMotion  = false;                        // [-]     - Linear cast CCD for fast shards or projectiles
    bool                       ParticleCollision = true;                         // [-]     - Exposes proxy for ParticleSpace collisions
    uint32_t                   InstanceTag       = 0u;                           // [id]    - Caller-owned instance or fragment index
    RigidShapeDescriptor       Shape             {};                             // [-]     - Collider shape & surface parameters
};

struct RigidBodySnapshot
{
    RigidBodyToken             Token                {};                   // [id]    - Body token
    uint32_t                   InstanceTag          = 0u;                 // [id]    - Caller-owned instance or fragment index
    Mathematics::Axis3D        PositionMeters       { 0.0f, 0.0f, 0.0f }; // [m]     - Current world position
    RigidOrientationQuaternion Orientation          {};                   // [-]     - Current world unit quaternion
    Mathematics::Axis3D        LinearVelocity       { 0.0f, 0.0f, 0.0f }; // [m/s]   - Current linear velocity
    Mathematics::Axis3D        AngularVelocity      { 0.0f, 0.0f, 0.0f }; // [rad/s] - Current angular velocity
    float                      MassKg               = 0.0f;               // [kg]    - Effective body mass (0 for static)
    float                      KineticEnergyJoules  = 0.0f;               // [J]     - Instantaneous translational + rotational kinetic energy
    float                      PeakContactImpulseNs = 0.0f;               // [N*s]   - Peak impulse experienced on last tick
    bool                       Awake                = false;              // [-]     - True while active in solver island
    bool                       Resident             = false;              // [-]     - True while registered in Jolt simulation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class RigidBodyDynamics
{
public:
    [[nodiscard]] static float                      EstimateShapeVolumeM3(const RigidShapeDescriptor& Shape) noexcept;
    [[nodiscard]] static float                      ResolveEffectiveMassKg(const RigidBodyDescriptor& Descriptor) noexcept;
    [[nodiscard]] static Mathematics::Scalar3D      EstimatePrincipalInertiaKgM2(
        const RigidShapeDescriptor& Shape,
        float                       MassKg) noexcept;
    [[nodiscard]] static RigidOrientationQuaternion NormalizeQuaternion(RigidOrientationQuaternion Quat) noexcept;
    [[nodiscard]] static Mathematics::Axis3D        RotateByQuaternion(
        RigidOrientationQuaternion Quat,
        Mathematics::Axis3D        Vector) noexcept;
    [[nodiscard]] static Mathematics::Axis3D        RotateByConjugateQuaternion(
        RigidOrientationQuaternion Quat,
        Mathematics::Axis3D        Vector) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
