/*============================================================================================================================================
                                                      COMPONENTINSTANCECATEGORIES.H
============================================================================================================================================*/
// 🧩 Lean POD component instance descriptors: SpatialOrigin, Geometry, Physics Constraints, Light, Camera, Collision, Vehicle

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassToken.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ComponentCategory : uint8_t
{
    SpatialOrigin      = 0u,  // [-] - Mandatory root transform origin
    TransformPivot     = 1u,  // [-] - Local transform pivot without visible geometry
    GeometryInstance   = 2u,  // [-] - Compiled .GeometrySpace / FCT1 surface with material slot overrides
    ConstraintInstance = 3u,  // [-] - Physics-backed Jolt two-body constraint replacing legacy sockets
    LightInstance      = 4u,  // [-] - PointEmission, SpotBeam, or AreaLightSource light source
    CameraInstance     = 5u,  // [-] - Perspective lens & frustum preview with Look-Through pilot mode
    CollisionVolume    = 6u,  // [-] - Box, Sphere, or Capsule Jolt collision / overlap proxy
    DirectionArrow     = 7u,  // [-] - Shaded +X orientation arrow visual
    VehicleDynamics       = 8u,  // [-] - Non-transform powertrain, torque curve, aero & wheel constraint driver
    RotationalMotion      = 9u,  // [-] - Non-transform continuous angular velocity driver
    RigidBodyInstance     = 10u, // [-] - JoltPhysics rigid body dynamics, mass, damping, axis locks & PhysicsMaterial
    SoftBodyInstance      = 11u, // [-] - XPBD / Neo-Hookean FEM deformable body referencing .SoftBody & optional .VertexAtlas
    ForceFieldInstance    = 12u, // [-] - Analytical force field (Wind, Radial, Vortex, Turbulence, Drag)
    FluidRegionInstance   = 13u, // [-] - 3D Eulerian / Sparse fluid volume release source (smoke, fire, fuel)
    ParticleCloudInstance = 14u, // [-] - GPU/CPU particle cloud emitter coupled to ForceFields & Fluid velocity
};

enum class PhysicsConstraintCategory : uint8_t
{
    Fixed  = 0u, // [-] - Rigid 6-DOF weld with optional strain rupture
    Hinge  = 1u, // [-] - Single-axis revolute joint with angular limits & Hold/Steer/Sway motor
    Slider = 2u, // [-] - Prismatic translation along axis with travel limits
    Ball   = 3u, // [-] - Swing-twist spherical joint bounded by cone & twist limits
    Spring = 4u, // [-] - Suspension axis spring-damper (natural frequency Hz & damping ratio zeta)
    Spin   = 5u, // [-] - Continuous motorized rotor / wheel hub spin (TargetRpm & torque)
    Tether = 6u, // [-] - Unilateral distance rope (slack under length, taut + break load at max)
    Dock   = 7u, // [-] - Key-filtered magnetic latch engaging within SnapRadius & releasing past BreakForceN
};

enum class ConstraintAxis : uint8_t
{
    AxisX = 0u, // [-] - Local +X axis
    AxisY = 1u, // [-] - Local +Y axis
    AxisZ = 2u, // [-] - Local +Z axis
};

enum class ConstraintDriveMode : uint8_t
{
    Hold      = 0u, // [-] - Hold equilibrium / spring rest
    Steer     = 1u, // [-] - Driven by steering angle input
    Sway      = 2u, // [-] - Periodic oscillation / test sweep
    Motor     = 3u, // [-] - Active velocity or position motor
    Freewheel = 4u, // [-] - Unpowered coasting spin
};

enum class LightSourceCategory : uint8_t
{
    PointEmission = 0u, // [-] - Omnidirectional inverse-square point light
    SpotBeam      = 1u, // [-] - Directional cone spotlight along +X
    AreaLightSource   = 2u, // [-] - Rectangular LTC area emitter
};

enum class CollisionVolumeCategory : uint8_t
{
    Box          = 0u, // [-] - Axis-aligned half-extents box
    Sphere       = 1u, // [-] - Uniform radius sphere
    Capsule      = 2u, // [-] - Axis-aligned radius + half-height capsule
    Cylinder     = 3u, // [-] - Axis-aligned radius + half-height cylinder
    ConvexHull   = 4u, // [-] - Baked convex polyhedron from .GeometrySpace mesh
    CompoundHull = 5u, // [-] - Multi-hull compound collider baked from .GeometrySpace mesh
    TriangleMesh = 6u, // [-] - Exact static triangle mesh collider (Static motion only)
};

enum class RigidBodyMotionKind : uint8_t
{
    Static    = 0u, // [-] - Immovable world rigid body
    Kinematic = 1u, // [-] - Scripted kinematic body
    Dynamic   = 2u, // [-] - Fully simulated Jolt dynamic body
};

enum class GeometryCollisionProfile : uint8_t
{
    BlockAll    = 0u, // [-] - Solid rigid body collider
    OverlapAll  = 1u, // [-] - Trigger sensor only
    NoCollision = 2u, // [-] - Render-only geometry without physics proxy
};

enum class CurveInterpolationForm : uint8_t
{
    Linear       = 0u, // [-] - Piecewise linear segments
    SmoothSpline = 1u, // [-] - Catmull-Rom smooth spline
    EaseInOut    = 2u, // [-] - Cosine ease between keys
    StepHold     = 3u, // [-] - Constant step hold
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComponentTransform
{
    Mathematics::Axis3D   LocationMeters  { 0.0f, 0.0f, 0.0f }; // [m]   - Local translation relative to EnclosingIndex
    Mathematics::Axis3D   RotationDegrees { 0.0f, 0.0f, 0.0f }; // [deg] - Local YZX Euler angles matching ComponentViewer
    Mathematics::Scalar3D ScaleFactors    { 1.0f, 1.0f, 1.0f }; // [1]   - Local scale multipliers
};

struct MaterialSlotOverride
{
    uint32_t              SlotIdentifier  = 0u;                 // [id]  - Target material slot index (e.g. CourseMaterial 6..15)
    Mathematics::Scalar3D AlbedoLinearRGB { 0.8f, 0.8f, 0.8f }; // [0-1] - Override linear RGB albedo
};

struct GeometryComponentSettings
{
    std::string                      AssetIdentifier     {};                                   // [-] - Compiled asset identifier (e.g. "ControlVehicleBody")
    int32_t                          ForcedLodLevel      = -1;                                 // [-] - -1 = Auto LOD; >=0 = pinned LOD level
    bool                             CastShadow          = true;                               // [-] - Participates in direct shadow depth
    GeometryCollisionProfile         CollisionProfile    = GeometryCollisionProfile::BlockAll; // [-] - Rigid collider generation policy
    std::vector<MaterialSlotOverride> SlotOverrides      {};                                   // [-] - Per-slot linear colour overrides
};

struct ConstraintComponentSettings
{
    PhysicsConstraintCategory Form                 = PhysicsConstraintCategory::Fixed; // [-]    - Active physics constraint kind
    ConstraintAxis        Axis                 = ConstraintAxis::AxisY;        // [-]    - Primary joint axis (X, Y, Z)
    ConstraintDriveMode   DriveMode            = ConstraintDriveMode::Hold;    // [-]    - Motor/input driver behaviour
    float                 MinAngleDegrees      = -32.0f;                       // [deg]  - Lower hinge limit
    float                 MaxAngleDegrees      = 32.0f;                        // [deg]  - Upper hinge limit
    float                 MinTravelMeters      = -0.09f;                       // [m]    - Lower slider/spring travel
    float                 MaxTravelMeters      = 0.09f;                        // [m]    - Upper slider/spring travel
    float                 ConeAngleDegrees     = 35.0f;                        // [deg]  - Ball swing half-cone angle
    float                 TwistLimitDegrees    = 45.0f;                        // [deg]  - Ball axial twist limit
    float                 NaturalFrequencyHz   = 2.00f;                        // [Hz]   - Spring stiffness frequency
    float                 DampingRatioZeta     = 0.42f;                        // [-]    - Spring damping ratio (zeta)
    float                 TravelEachWayMeters  = 0.090f;                       // [m]    - Symmetric suspension stroke
    float                 TargetRpm            = 600.0f;                       // [rpm]  - Spin target angular velocity
    float                 MaxMotorTorqueNm     = 400.0f;                       // [N*m]  - Spin/hinge actuator torque ceiling
    float                 RopeLengthMeters     = 0.32f;                        // [m]    - Tether maximum length
    float                 BreakForceNewtons    = 3500.0f;                      // [N]    - Rupture / Dock release force threshold
    float                 BreakTorqueNm        = 0.0f;                         // [N*m]  - Flexural rupture torque (0 = disabled)
    float                 SnapRadiusMeters     = 0.35f;                        // [m]    - Dock re-latch capture radius
    float                 NormalizedEditPose   = 0.50f;                        // [0-1]  - Edit-mode kinematic preview slider
    std::string           DockKey              {};                             // [-]    - Required nested Tag to allow Dock latch
    bool                  DockConnected        = true;                         // [-]    - Initial / current Dock latch engagement
    bool                  DisableSelfCollide   = true;                         // [-]    - Suppresses contact pairs between joined bodies
};

struct LightComponentSettings
{
    LightSourceCategory      Form                   = LightSourceCategory::SpotBeam; // [-]   - PointEmission, SpotBeam, or AreaLightSource
    Mathematics::Scalar3D LinearColourRGB        { 1.0f, 0.96f, 0.88f };       // [0-1] - Linear RGB emitter chromaticity
    float                 IntensityCandela       = 1200.0f;                    // [cd]  - Luminous intensity
    float                 AttenuationRangeMeters = 12.0f;                      // [m]   - Cutoff attenuation radius
    float                 ConeAngleDegrees       = 28.0f;                      // [deg] - Outer half-cone angle (SpotBeam)
    float                 PenumbraSoftness       = 0.35f;                      // [0-1] - Angular falloff softness
    bool                  CastShadow             = false;                      // [-]   - Enables shadow map contribution
    std::string           IesProfileIdentifier   {};                           // [-]   - Optional LM-63 .ies luminaire profile
};

struct CameraComponentSettings
{
    float VerticalFovDegrees = 52.0f;   // [deg] - Vertical field of view
    float NearClipMeters     = 0.10f;   // [m]   - Near frustum distance
    float FarClipMeters      = 5000.0f; // [m]   - Far frustum distance
    float AspectRatio        = 16.0f / 9.0f; // [-] - Preview frustum aspect ratio
};

struct CollisionVolumeSettings
{
    CollisionVolumeCategory Form                     = CollisionVolumeCategory::Box; // [-]  - Box, Sphere, Capsule, Cylinder, ConvexHull, CompoundHull, TriangleMesh
    Mathematics::Scalar3D   HalfExtentsMeters        { 0.95f, 0.24f, 0.28f };        // [m]  - Box half-extents
    float                   RadiusMeters             = 0.35f;                        // [m]  - Sphere / Capsule / Cylinder radius
    float                   HalfHeightMeters         = 0.45f;                        // [m]  - Capsule / Cylinder half-height
    float                   ConvexMarginMeters       = 0.015f;                       // [m]  - Convex hull bevel margin
    uint16_t                CollisionChannel         = 1u;                           // [id] - Broadphase layer channel
    uint32_t                CollideWithMask          = 0xFFFFFFFFu;                  // [mask]- Bitmask of channels this collider interacts with
    uint16_t                PhysicsMaterialId        = 0u;                           // [id] - Registered PhysicsMaterial slot (0 = Asphalt)
    uint32_t                MaxCompoundHulls         = 4u;                           // [cnt]- Max sub-hulls when baking CompoundHull from .GeometrySpace
    bool                    TriggerOverlapOnly       = false;                        // [-]  - Overlap sensor publishing TriggerOverlapReading without impulse
    std::string             SourceGeometryIdentifier {};                             // [-]  - Optional .GeometrySpace asset key for ConvexHull/CompoundHull/TriangleMesh
};

struct RigidBodyComponentSettings
{
    RigidBodyMotionKind   MotionKind           = RigidBodyMotionKind::Dynamic; // [-]      - Static, Kinematic, or Dynamic Jolt body
    float                 ExplicitMassKg       = 0.0f;                         // [kg]     - >0 overrides density * volume
    float                 DensityKgPerM3       = 2350.0f;                      // [kg/m^3] - Material bulk density when ExplicitMassKg == 0
    Mathematics::Axis3D   CenterOfMassOffsetM  { 0.0f, 0.0f, 0.0f };           // [m]      - Local center-of-mass shift
    float                 LinearDamping        = 0.02f;                        // [1/s]    - Linear drag damping
    float                 AngularDamping       = 0.05f;                        // [1/s]    - Angular drag damping
    float                 GravityFactor        = 1.0f;                         // [-]      - Gravity multiplier
    uint16_t              CollisionChannel     = 1u;                           // [id]     - Primary broadphase layer channel
    uint32_t              CollideWithMask      = 0xFFFFFFFFu;                  // [mask]   - Layer interaction mask
    uint16_t              PhysicsMaterialId    = 0u;                           // [id]     - Registered PhysicsMaterial identifier
    uint8_t               FreezeAxisMask       = 0u;                           // [mask]   - Bits 0..2 freeze X/Y/Z; bits 3..5 freeze RotX/RotY/RotZ
    bool                  AllowSleep           = true;                         // [-]      - Permits Jolt island sleep at rest
    bool                  ContinuousMotion     = false;                        // [-]      - Linear-cast CCD for fast projectiles/vehicles
};

struct DirectionArrowSettings
{
    float                 LengthMeters    = 0.75f;                // [m]   - Shaded arrow length along +X
    Mathematics::Scalar3D LinearColourRGB { 0.98f, 0.82f, 0.24f }; // [0-1] - Shaded solid colour
};

struct EngineTorqueSample
{
    float Rpm      = 800.0f; // [rpm] - Engine crankshaft speed
    float TorqueNm = 230.0f; // [N*m] - Output shaft torque
};

struct VehicleDynamicsSettings
{
    float                               ChassisMassKg        = 1300.0f;                               // [kg]    - Total vehicle sprung mass
    float                               WheelRadiusMeters    = 0.34f;                                 // [m]     - Nominal tyre outer radius
    float                               IdleRpm              = 850.0f;                                // [rpm]   - Minimum engine idle speed
    float                               RedlineRpm           = 7200.0f;                               // [rpm]   - Engine limiter threshold
    float                               DragCoefficient      = 0.31f;                                 // [-]     - Longitudinal aerodynamic drag Cd
    float                               FrontalAreaM2        = 2.15f;                                 // [m^2]   - Reference frontal area
    float                               AeroLiftAreaM2       = 1.40f;                                 // [m^2]   - Downforce lift area feeding RearWing load
    float                               FinalDriveRatio      = 3.72f;                                 // [-]     - Differential final drive ratio
    std::array<float, 6u>               GearRatios           { 3.36f, 2.07f, 1.43f, 1.09f, 0.87f, 0.72f }; // [-] - 6-speed gearbox ratios
    CurveInterpolationForm              Interpolation        = CurveInterpolationForm::SmoothSpline;  // [-]     - Torque curve interpolation law
    std::vector<EngineTorqueSample>     TorqueCurve          {};                                      // [-]     - Ordered [rpm, N*m] curve points
    std::vector<ComponentInstanceToken> BoundWheelTokens     {};                                      // [-]     - Tyre/wheel GeometryInstances driven by solver
};

struct RotationalMotionSettings
{
    float                  RateDegreesPerSecond = 45.0f; // [deg/s] - Angular spin speed around +Y
    ComponentInstanceToken TargetInstance       {};      // [-]     - Target scene instance (invalid = SpatialOrigin)
};

struct SoftBodyComponentSettings
{
    std::string SoftBodyAssetPath   { "EngineContent/Simulation/Deformables/WovenCloth.SoftBody" }; // [-] - .SoftBody asset path
    std::string VertexAtlasPath     {};                                                             // [-] - Optional .VertexAtlas path
    float       SleepDistanceMeters = 60.0f;                                                        // [m] - World LOD/sleep distance
};

struct ForceFieldComponentSettings
{
    uint8_t               ModelCode    = 0u;                   // [-]   - ForceModelOrdinal code (0=DirectionalWind, 3=RotationalColumn)
    Mathematics::Axis3D   Direction    { 1.0f, 0.0f, 0.0f };   // [1]   - Primary force axis
    float                 Strength     = 4.0f;                 // [m/s2]- Force intensity
    float                 RadiusMeters = 8.0f;                 // [m]   - Influence radius
    uint8_t               FalloffCode  = 3u;                   // [-]   - ForceFalloffOrdinal code (0=SmoothStep, 3=Constant)
};

struct FluidRegionComponentSettings
{
    float               RadiusMeters = 0.42f;                  // [m]   - Spherical release radius
    Mathematics::Axis3D VelocityMps  { 0.0f, 2.2f, 0.0f };     // [m/s] - Injected fluid velocity
    float               Density      = 1.50f;                  // [-]   - Injected smoke density
    float               Temperature  = 2.60f;                  // [-]   - Injected thermal buoyancy
    float               Fuel         = 1.10f;                  // [-]   - Injected combustion fuel
};

struct ParticleCloudComponentSettings
{
    float    RadiusMeters   = 0.36f;                           // [m]   - Emitter sphere radius
    uint32_t ParticleCount  = 256u;                            // [cnt] - Maximum particle budget
    float    ReleaseRate    = 12.0f;                           // [1/s] - Spawn rate per second
    float    SpeedMps       = 1.50f;                           // [m/s] - Initial ejection speed
    float    LifetimeSec    = 2.40f;                           // [s]   - Particle lifetime
    float    GravityY       = 0.15f;                           // [m/s2]- Vertical buoyancy/gravity acceleration
    float    GasInfluence   = 0.85f;                           // [0-1] - Fluid velocity advection coupling weight
    float    FieldInfluence = 0.85f;                           // [0-1] - ForceField acceleration coupling weight
};

struct ComponentInstanceHeader
{
    ComponentInstanceToken Token              {};                              // [id]   - Generation-checked instance identity
    ComponentCategory      Category           = ComponentCategory::TransformPivot; // [-] - Typed component category
    std::string            Title              {};                              // [-]    - Unique instance name inside ComponentClass
    uint32_t               EnclosingIndex     = InvalidNestingIndex;         // [idx]  - Enclosing parent index in flat storage
    uint32_t               FirstNestedIndex   = InvalidNestingIndex;         // [idx]  - First nested instance index
    uint32_t               NextPeerIndex      = InvalidNestingIndex;         // [idx]  - Next sibling peer index under same EnclosingIndex
    uint16_t               DepthLevel         = 0u;                            // [cnt]  - Hierarchy indentation depth (0 = SpatialOrigin)
    uint32_t               CategorySlotIndex  = 0u;                            // [idx]  - Index inside typed category array
    ComponentTransform     LocalTransform     {};                              // [-]    - Local transform (unused for Behaviour categories)
    bool                   VisibilityEnabled  = true;                          // [-]    - Viewport visibility toggle
    bool                   HiddenInPlay       = false;                         // [-]    - Hidden during runtime play / simulation
    std::string            TagCsv             {};                              // [-]    - Comma-separated tags (checked by DockKey)
};

[[nodiscard]] constexpr bool QueryCategoryHasTransform(ComponentCategory Category) noexcept
{
    return Category != ComponentCategory::VehicleDynamics && Category != ComponentCategory::RotationalMotion;
}

[[nodiscard]] constexpr const char* QueryCategoryLabel(ComponentCategory Category) noexcept
{
    switch (Category)
    {
        case ComponentCategory::SpatialOrigin:      return "Spatial Origin";
        case ComponentCategory::TransformPivot:     return "Transform Pivot";
        case ComponentCategory::GeometryInstance:   return "Geometry";
        case ComponentCategory::ConstraintInstance: return "Constraint";
        case ComponentCategory::LightInstance:      return "Light";
        case ComponentCategory::CameraInstance:     return "Camera";
        case ComponentCategory::CollisionVolume:    return "Collision Volume";
        case ComponentCategory::DirectionArrow:     return "Direction Arrow";
        case ComponentCategory::VehicleDynamics:       return "Vehicle Dynamics";
        case ComponentCategory::RotationalMotion:      return "Rotational Motion";
        case ComponentCategory::RigidBodyInstance:     return "Rigid Body";
        case ComponentCategory::SoftBodyInstance:      return "Soft Body";
        case ComponentCategory::ForceFieldInstance:    return "Force Field";
        case ComponentCategory::FluidRegionInstance:   return "Fluid Region";
        case ComponentCategory::ParticleCloudInstance: return "Particle Cloud";
    }
    return "Component";
}

[[nodiscard]] constexpr const char* QueryConstraintCategoryLabel(PhysicsConstraintCategory Form) noexcept
{
    switch (Form)
    {
        case PhysicsConstraintCategory::Fixed:  return "Fixed";
        case PhysicsConstraintCategory::Hinge:  return "Hinge";
        case PhysicsConstraintCategory::Slider: return "Slider";
        case PhysicsConstraintCategory::Ball:   return "Ball";
        case PhysicsConstraintCategory::Spring: return "Spring";
        case PhysicsConstraintCategory::Spin:   return "Spin";
        case PhysicsConstraintCategory::Tether: return "Tether";
        case PhysicsConstraintCategory::Dock:   return "Dock";
    }
    return "Constraint";
}

} // namespace Frontier::ComponentClass
