/*============================================================================================================================================
                                                            BROADPHASESOLVER.H
============================================================================================================================================*/
// 🧩 Broadphase collision channels, raycast/overlap queries, and 80-byte GPU/CPU RigidColliderProxy analytical sphere solver

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <array>
#include <cstdint>
#include <span>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class CollisionLayerChannel : uint16_t
{
    StaticEnvironment = 0u, // [-] - Immovable ground, walls, and architecture
    DynamicSolid      = 1u, // [-] - Interactive rigid bodies
    FractureFragment  = 2u, // [-] - Shards & debris produced by fracture partitioning
    SensorTrigger     = 3u, // [-] - Overlap-only volume triggers
    DebrisFragment    = 4u, // [-] - Lightweight granular debris solids
    Count             = 5u, // [count] - Total broadphase layer channels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RayCastQuery
{
    Mathematics::Axis3D OriginMeters     { 0.0f, 0.0f, 0.0f }; // [m]    - Ray origin in world space
    Mathematics::Axis3D DirectionUnit    { 1.0f, 0.0f, 0.0f }; // [-]    - Ray unit direction
    float               MaxDistanceMeters = 100.0f;            // [m]    - Maximum cast length
    uint32_t            ChannelMask       = 0xFFFFFFFFu;       // [mask] - Bitmask of CollisionLayerChannel ordinals
};

struct RayCastHitReading
{
    bool                HitDetected    = false;                // [-]   - True if ray hit a body
    RigidBodyToken      HitBody        {};                     // [id]  - Token of hit rigid body
    uint16_t            HitMaterialId  = 0u;                   // [id]  - PhysicsMaterial identifier of hit body
    float               Fraction       = 1.0f;                 // [0-1] - Normalized hit distance along ray
    float               DistanceMeters = 0.0f;                 // [m]   - Hit distance from ray origin
    Mathematics::Axis3D WorldPoint     { 0.0f, 0.0f, 0.0f };   // [m]   - World-space impact position
    Mathematics::Axis3D SurfaceNormal  { 0.0f, 0.0f, 1.0f };   // [-]   - World-space outward surface normal
};

struct ShapeCastQuery
{
    CollisionShapeCategory     ShapeCategory     = CollisionShapeCategory::Cylinder; // [-]    - Sphere, Box, Capsule, or Cylinder
    Mathematics::Axis3D        StartMeters       { 0.0f, 0.0f, 0.0f };               // [m]    - Sweep start center in world space
    RigidOrientationQuaternion Orientation       {};                                 // [-]    - World orientation of swept shape
    Mathematics::Axis3D        DisplacementMeters{ 0.0f, -1.0f, 0.0f };              // [m]    - Sweep displacement vector (end - start)
    Mathematics::Scalar3D      HalfExtentsMeters { 0.25f, 0.25f, 0.25f };            // [m]    - Half-extents when ShapeCategory == Box
    float                      RadiusMeters      = 0.35f;                            // [m]    - Radius for Sphere / Capsule / Cylinder
    float                      HalfHeightMeters  = 0.15f;                            // [m]    - Half-height for Capsule / Cylinder
    uint32_t                   ChannelMask       = 0xFFFFFFFFu;                      // [mask] - Bitmask of CollisionLayerChannel ordinals
};

struct ShapeCastHitReading
{
    bool                HitDetected       = false;              // [-]   - True if sweep intersected a body
    RigidBodyToken      HitBody           {};                   // [id]  - Token of hit rigid body
    uint16_t            HitMaterialId     = 0u;                 // [id]  - PhysicsMaterial identifier of hit body
    float               Fraction          = 1.0f;               // [0-1] - Normalized sweep fraction along DisplacementMeters
    float               PenetrationDepthM = 0.0f;               // [m]   - Initial penetration depth when starting overlapped
    Mathematics::Axis3D WorldPoint        { 0.0f, 0.0f, 0.0f }; // [m]   - World-space contact point on target body
    Mathematics::Axis3D SurfaceNormal     { 0.0f, 1.0f, 0.0f }; // [-]   - Outward surface normal on target body
};

struct SphereOverlapQuery
{
    Mathematics::Axis3D CenterMeters  { 0.0f, 0.0f, 0.0f }; // [m]    - Overlap sphere world center
    float               RadiusMeters  = 1.0f;               // [m]    - Overlap sphere radius
    uint32_t            ChannelMask   = 0xFFFFFFFFu;        // [mask] - Bitmask of CollisionLayerChannel ordinals
};

struct BoxOverlapQuery
{
    Mathematics::Axis3D        CenterMeters      { 0.0f, 0.0f, 0.0f }; // [m]    - Overlap box world center
    Mathematics::Scalar3D      HalfExtentsMeters { 0.5f, 0.5f, 0.5f }; // [m]    - Overlap box half-extents
    RigidOrientationQuaternion Orientation       {};                   // [-]    - Overlap box orientation
    uint32_t                   ChannelMask       = 0xFFFFFFFFu;        // [mask] - Bitmask of CollisionLayerChannel ordinals
};

inline constexpr uint32_t MaxPublishedTriggerOverlaps = 64u; // [cnt] - Fixed wait-free trigger overlap buffer capacity

enum class TriggerOverlapPhase : uint8_t
{
    Entered = 0u, // [-] - First step where OtherBody overlapped SensorBody
    Staying = 1u, // [-] - Persisting overlap across consecutive physics steps
    Exited  = 2u, // [-] - First step after OtherBody separated from SensorBody
};

struct TriggerOverlapReading
{
    RigidBodyToken      SensorBody        {};                   // [id]  - Trigger sensor body token
    RigidBodyToken      OtherBody         {};                   // [id]  - Overlapping solid body token
    uint32_t            SensorInstanceTag = 0u;                 // [id]  - Caller tag on sensor body
    uint32_t            OtherInstanceTag  = 0u;                 // [id]  - Caller tag on overlapping body
    TriggerOverlapPhase Phase             = TriggerOverlapPhase::Entered; // [-] - Enter / Stay / Exit phase
    Mathematics::Axis3D ContactPoint      { 0.0f, 0.0f, 0.0f }; // [m]   - Representative overlap contact point
    uint64_t            StepIndex         = 0ull;               // [idx] - Physics step when observed
};

struct TriggerOverlapListReading
{
    uint64_t                                                     Generation     = 0ull; // [cnt] - Publication generation
    uint64_t                                                     CompletedSteps = 0ull; // [cnt] - Physics step index
    uint32_t                                                     OverlapCount   = 0u;   // [cnt] - Valid entries in Overlaps[0..OverlapCount)
    uint32_t                                                     EnteredCount   = 0u;   // [cnt] - Entered events on this step
    uint32_t                                                     ExitedCount    = 0u;   // [cnt] - Exited events on this step
    std::array<TriggerOverlapReading, MaxPublishedTriggerOverlaps> Overlaps     {};     // [-]   - Wait-free readable trigger events
};

/// Compact 80-byte GPU/CPU collider proxy exported from Jolt rigid bodies.
struct alignas(16) RigidColliderProxy
{
    float    CenterMeters[3]      = { 0.0f, 0.0f, 0.0f };       // [m]    - World body origin
    uint32_t ShapeCode            = 0u;                         // [enum] - 0=Sphere, 1=OrientedBox, 2=Capsule(+Z)
    float    VelocityMps[3]       = { 0.0f, 0.0f, 0.0f };       // [m/s]  - World linear velocity
    float    RadiusMeters         = 0.5f;                       // [m]    - Sphere/Capsule radius or convex bevel
    float    HalfExtentsMeters[3] = { 0.5f, 0.5f, 0.5f };       // [m]    - Box half-extents (or Capsule Z half-height in [2])
    float    Restitution          = 0.25f;                      // [0-1]  - Surface bounce restitution
    float    OrientationQuat[4]   = { 0.0f, 0.0f, 0.0f, 1.0f }; // [-]    - World orientation quaternion (X,Y,Z,W)
    float    Friction             = 0.5f;                       // [-]    - Surface Coulomb friction
    uint32_t BodySlotIndex        = 0u;                         // [idx]  - 1-based RigidBodyToken::SlotIndex
    uint32_t ChannelMask          = 1u;                         // [mask] - 1u << CollisionChannel
    uint32_t DynamicResponseBit   = 0u;                         // [0/1]  - 1 when Dynamic (eligible for two-way reaction impulse)
};

static_assert(sizeof(RigidColliderProxy) == 80u, "RigidColliderProxy must occupy exactly 80 bytes for GPU SSBO parity.");
static_assert(alignof(RigidColliderProxy) == 16u, "RigidColliderProxy must remain 16-byte aligned.");

struct ColliderContactFeedback
{
    bool                Collided        = false;                // [-]   - True if sphere intersected proxy
    uint32_t            BodySlotIndex   = 0u;                   // [idx] - Hit rigid body slot index
    uint32_t            DynamicResponse = 0u;                   // [0/1] - True when dynamic rigid body should receive reaction impulse
    Mathematics::Axis3D ContactPoint    { 0.0f, 0.0f, 0.0f };   // [m]   - World contact position
    Mathematics::Axis3D ContactNormal   { 0.0f, 0.0f, 1.0f };   // [-]   - Outward normal from rigid body into sphere
    Mathematics::Axis3D SphereImpulseNs { 0.0f, 0.0f, 0.0f };   // [N*s] - Impulse applied to sphere (negated for rigid body)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class BroadphaseSolver
{
public:
    [[nodiscard]] static constexpr uint32_t ChannelBit(CollisionLayerChannel Channel) noexcept
    {
        return 1u << static_cast<uint32_t>(Channel);
    }

    /// Resolves single analytical sphere vs. a RigidColliderProxy (Sphere, Oriented Box, or Oriented +Z Capsule).
    [[nodiscard]] static ColliderContactFeedback ResolveSphereAgainstProxy(
        const RigidColliderProxy& Proxy,
        float                     SphereRadiusMeters,
        float                     SphereMassKg,
        float                     SpherePositionMeters[3],
        float                     SphereVelocityMps[3]) noexcept;

    /// Evaluates a sphere against a span of exported Jolt RigidColliderProxies.
    [[nodiscard]] static ColliderContactFeedback ResolveSphereAgainstProxies(
        std::span<const RigidColliderProxy> Proxies,
        float                               SphereRadiusMeters,
        float                               SphereMassKg,
        float                               SpherePositionMeters[3],
        float                               SphereVelocityMps[3]) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
