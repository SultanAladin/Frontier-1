/*============================================================================================================================================
                                                           FORCEMODELTYPES.H
============================================================================================================================================*/
// 🧩 Analytic force model enumerations, descriptor layouts, node state, and 64-byte GPU SSBO record
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kForceModelCount             = 20u;       // [-] - Total analytic force model ordinals (0..19)
inline constexpr uint32_t kForceFalloffCount           = 7u;        // [-] - Total falloff curve ordinals (0..6)
inline constexpr uint32_t kForceCategoryCount          = 7u;        // [-] - Force browser categories
inline constexpr uint32_t kMaximumReferenceForceModels = 16u;       // [-] - WebGL2 uniform cap in ForceGeneratorCatalog.js L9
inline constexpr uint32_t kMaximumGpuForceModels       = 256u;      // [-] - Vulkan SSBO capacity for simultaneous active force models
inline constexpr uint32_t kMaximumReferenceSources     = 6u;        // [-] - WebGL2 source uniform cap in ForceGeneratorCatalog.js L10
inline constexpr uint32_t kMaximumReferenceColliders   = 4u;        // [-] - WebGL2 sphere collider cap in ForceGeneratorCatalog.js L11
inline constexpr uint32_t kMaximumGridTiles            = 5u;        // [-] - Maximum sliding world-grid tiles per axis (L243)
inline constexpr uint32_t kMaximumSparseTileSlots      = 160u;      // [-] - Sparse infinite tile slot budget cap (L245)
inline constexpr uint32_t kMaximumInfiniteCells        = 1600000u;  // [-] - Sparse infinite cell budget cap (L246)
inline constexpr float    kDefaultFadeSeconds          = 0.15f;     // [s] - Default timing envelope fade duration (L233)

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

/// Numeric GLSL / Slang codes matching Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js & FluidShaders.js L294-L493.
enum class ForceModelOrdinal : uint32_t
{
    DirectionalWind     = 0u,   // [-] - Uniform directional push with ValueNoise gust modulation
    PointAttractor      = 1u,   // [-] - Radial pull toward centre with softened core
    PointRepulsor       = 2u,   // [-] - Radial push outward from centre with softened core
    RotationalColumn    = 3u,   // [-] - Tangential swirl + inward pull + axial lift (UI: "Vortex Column")
    CurlNoiseTurbulence = 4u,   // [-] - Divergence-free 3D CurlNoise agitation
    DragBaffle          = 5u,   // [-] - Velocity-proportional damping brake
    RadialBurst         = 6u,   // [-] - Periodic outward spherical pulse
    ThermalUpdraft      = 7u,   // [-] - Cylindrical axial lift column
    AxisAttractor       = 8u,   // [-] - Radial pull toward an infinite axis line
    RotationalRing      = 9u,   // [-] - Toroidal circulation ring (UI: "Vortex Ring")
    Orbit               = 10u,  // [-] - Tangential orbital circulation with centripetal pull
    Containment         = 11u,  // [-] - Restoring spherical wall inside reach radius
    ShockwaveShell      = 12u,  // [-] - Expanding spherical pressure front per period
    Implosion           = 13u,  // [-] - Periodic inward collapse toward centre
    DirectionalBurst    = 14u,  // [-] - Periodic axial impulse jet
    BrownianJitter      = 15u,  // [-] - Cell-quantized Hash33 re-rolled agitation
    DirectedStream      = 16u,  // [-] - Soft velocity-steering conveyor along axis (UI: "Flow Stream")
    WindShear           = 17u,  // [-] - Height-twisted rotating crosswind along axis
    Spin                = 18u,  // [-] - Solid-body tangential rotation around axis
    MagneticDipole      = 19u,  // [-] - Dipole field-line alignment looping pole to pole
};

/// Numeric GLSL / Slang falloff codes matching ForceGeneratorCatalog.js L13 & FluidShaders.js L305-L316.
enum class ForceFalloffOrdinal : uint32_t
{
    SmoothStep    = 0u, // [-] - 1 - smoothstep(0, 1, d)
    Linear        = 1u, // [-] - 1 - d
    InverseSquare = 2u, // [-] - (1 / (1 + 16 d^2)) * (1 - smoothstep(0.85, 1, d))
    Constant      = 3u, // [-] - 1 (infinite reach)
    Gaussian      = 4u, // [-] - exp(-5 d^2) * (1 - smoothstep(0.9, 1, d))
    Plateau       = 5u, // [-] - 1 - smoothstep(0.55, 1, d)
    Cosine        = 6u, // [-] - 0.5 + 0.5 * cos(pi * d)
};

enum class ForceCategoryOrdinal : uint8_t
{
    Directional = 0u, // [-] - Directional pushes, shear, streams
    Attraction  = 1u, // [-] - Point/axis attractors, orbit, containment, dipole
    Rotational  = 2u, // [-] - Rotational columns, rings, solid spin
    Turbulence  = 3u, // [-] - Curl noise, Brownian jitter
    Burst       = 4u, // [-] - Radial/directional pulses, shockwave, implosion
    Damping     = 5u, // [-] - Velocity drag baffles
    Thermal     = 6u, // [-] - Cylindrical updrafts
};

enum class ForceTargetChannelBits : uint32_t
{
    None     = 0u,      // [-] - Disabled across all consumers
    Gas      = 1u << 0, // [-] - Evaluated by Eulerian fluid gas solver
    Particle = 1u << 1, // [-] - Evaluated by GPU particle & XPBD solvers
    Rigid    = 1u << 2, // [-] - Evaluated by JoltPhysics dynamic bodies & vehicles
    All      = Gas | Particle | Rigid,
};

enum class ReleaseSourceShapeOrdinal : uint8_t
{
    Sphere = 0u, // [-] - Spherical injection volume
    Disc   = 1u, // [-] - Thin circular disc oriented by normal
    Box    = 2u, // [-] - Axis-aligned cubic injection volume
};

enum class FluidBoundaryOrdinal : uint8_t
{
    ClosedBox = 0u, // [-] - All 6 faces enforce no-penetration
    OpenTop   = 1u, // [-] - Top +Y face open outflow; sides and floor closed
    OpenAll   = 2u, // [-] - All 6 faces open outflow
};

enum class FluidAdvectionOrdinal : uint8_t
{
    SemiLagrangian = 0u, // [-] - RK2 midpoint semi-Lagrangian trace
    MacCormack     = 1u, // [-] - Selle 2008 overshoot-fallback MacCormack
};

enum class FluidGridModeOrdinal : uint8_t
{
    Fixed    = 0u, // [-] - Single or multi-tile sliding box region
    Infinite = 1u, // [-] - Unbounded sparse cubic tile lattice
};

enum class FluidFollowOrdinal : uint8_t
{
    None      = 0u, // [-] - Stationary world origin
    Source    = 1u, // [-] - Follows active release source (TOML: "Emitter")
    Particles = 2u, // [-] - Follows active particle cloud
    Camera    = 3u, // [-] - Follows viewport camera
};

enum class SourceMotionPathOrdinal : uint8_t
{
    None     = 0u, // [-] - Static position
    Circle   = 1u, // [-] - Circular horizontal orbit
    Line     = 2u, // [-] - Sinusoidal X oscillation
    Figure8  = 3u, // [-] - Lemniscate XZ path
    Straight = 4u, // [-] - Unbounded +X linear drive
    Slalom   = 5u, // [-] - Unbounded +X drive with Z weave
};

enum class DebrisCategoryOrdinal : uint8_t
{
    Rubble = 0u, // [-] - Heavy ballistic chunks
    Ember  = 1u, // [-] - Glowing light sparks
    Paper  = 2u, // [-] - High-drag fluttering sheets
};

enum class ColourRampOrdinal : uint8_t
{
    Turbo       = 0u, // [-] - Perceptual rainbow gradient
    Engineering = 1u, // [-] - Blue-teal-yellow-red engineering gradient
    Plasma      = 2u, // [-] - Indigo-magenta-yellow plasma gradient
    Diverging   = 3u, // [-] - Blue-white-red signed diverging gradient
};

enum class ScalarChannelOrdinal : uint8_t
{
    Speed          = 0u, // [m/s] - Velocity magnitude
    Pressure       = 1u, // [Pa]  - Solved pressure scalar
    Vorticity      = 2u, // [1/s] - Curl magnitude
    Temperature    = 3u, // [-]   - Gas temperature
    Density        = 4u, // [-]   - Smoke density
    Divergence     = 5u, // [1/s] - Pre-projection velocity divergence
    ForceMagnitude = 6u, // [m/s^2] - Analytic force acceleration magnitude
};

enum class VolumeFilterOrdinal : uint8_t
{
    Smooth = 0u, // [-] - Trilinear hardware sampling
    Voxel  = 1u, // [-] - Nearest-cell voxel preview
};

enum class VectorDisplayOrdinal : uint8_t
{
    PlaneSection = 0u, // [-] - Single orthogonal slice grid (TOML: "Slice")
    VolumeGrid   = 1u, // [-] - Full 3D strided vector lattice (TOML: "Volume")
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ForceModelDefaults
{
    float                Strength     = 0.0f;                                   // [m/s^2] - Default acceleration / speed scale
    float                RadiusMeters = 1.0f;                                   // [m]     - Default influence radius
    std::array<float, 3> Direction    = { 0.0f, 1.0f, 0.0f };                   // [-]     - Default axis direction
    ForceFalloffOrdinal  Falloff      = ForceFalloffOrdinal::SmoothStep;        // [-]     - Default radial falloff curve
    float                Frequency    = 0.0f;                                   // [-]     - Primary model-specific parameter
    float                Secondary    = 0.0f;                                   // [-]     - Secondary model-specific parameter
    float                Tertiary     = 0.0f;                                   // [-]     - Tertiary model-specific parameter (start delay)
};

struct ForceParameterLabels
{
    std::string_view Frequency {}; // [-] - Inspector label for Frequency (empty when unused)
    std::string_view Secondary {}; // [-] - Inspector label for Secondary (empty when unused)
    std::string_view Tertiary  {}; // [-] - Inspector label for Tertiary (empty when unused)
};

struct ForceModelDescriptor
{
    std::string_view     TomlId      {};                                        // [-] - Exact reference TOML/JS Id (e.g., "VortexColumn")
    ForceModelOrdinal    Code        = ForceModelOrdinal::DirectionalWind;      // [-] - Exact GLSL / Slang type code (0..19)
    ForceCategoryOrdinal Category    = ForceCategoryOrdinal::Directional;       // [-] - Browser category ordinal
    std::string_view     CategoryId  {};                                        // [-] - Exact reference category string
    std::string_view     DisplayName {};                                        // [-] - Exact user-facing UI name
    std::string_view     IconToken   {};                                        // [-] - Reference icon token
    std::string_view     TintHex     {};                                        // [-] - Reference UI tint hex (#rrggbb)
    std::string_view     Summary     {};                                        // [-] - Reference tooltip / drawer summary
    ForceModelDefaults   Defaults    {};                                        // [-] - Reference default parameters
    ForceParameterLabels Labels      {};                                        // [-] - Reference inspector parameter labels
    bool                 GasVelocitySaturated = true;                           // [-] - True if ForcesFragment applies uForceResponse saturation
};

struct ForceModelNode
{
    std::string          NodeId          {};                                    // [-]     - Unique scene node identifier
    ForceModelOrdinal    ModelCode       = ForceModelOrdinal::DirectionalWind;  // [-]     - Analytic model ordinal (0..19)
    std::string          Name            {};                                    // [-]     - User-editable display label
    bool                 Enabled         = true;                                // [-]     - Active in simulation evaluation
    bool                 Visible         = true;                                // [-]     - Gizmo visibility toggle
    std::array<float, 3> PositionMeters  = { 0.0f, 0.0f, 0.0f };                // [m]     - World-space centre
    std::array<float, 3> Direction       = { 0.0f, 1.0f, 0.0f };                // [-]     - Axis / heading vector
    float                Strength        = 6.0f;                                // [m/s^2] - Base force strength
    float                RadiusMeters    = 2.0f;                                // [m]     - Influence radius
    ForceFalloffOrdinal  Falloff         = ForceFalloffOrdinal::Constant;       // [-]     - Radial falloff curve
    float                Frequency       = 0.6f;                                // [-]     - Model parameter 1 (Frequency / Period / TargetSpeed)
    float                Secondary       = 0.35f;                               // [-]     - Model parameter 2 (Secondary / Width / Core)
    float                Tertiary        = 0.0f;                                // [-]     - Model parameter 3 (Start Delay)
    float                StartTimeSec    = 0.0f;                                // [s]     - Envelope start time
    float                DurationSec     = 0.0f;                                // [s]     - Envelope active duration (0 = infinite)
    float                FadeSec         = kDefaultFadeSeconds;                 // [s]     - Envelope fade-in / fade-out ramp
    float                RepeatSec       = 0.0f;                                // [s]     - Envelope repeat period (0 = one-shot)
    uint32_t             TargetChannels  = static_cast<uint32_t>(ForceTargetChannelBits::All); // [-] - Consumer channel mask
};

/// 64-byte GPU-aligned record matching FluidShaders.js uForceA/B/C/D packing verbatim.
struct alignas(16) ForceModelGpuRecord
{
    float PositionRadius[4]    = { 0.0f, 0.0f, 0.0f, 1.0f }; // [m]   - xyz = world position, w = radius
    float DirectionStrength[4] = { 0.0f, 1.0f, 0.0f, 0.0f }; // [-]   - xyz = direction, w = strength * timingWeight
    float TypeFalloffParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // [-]   - x = type code, y = falloff code, z = frequency, w = secondary
    float TimingChannelMask[4] = { 0.0f, 0.0f, 7.0f, 0.0f }; // [-]   - x = tertiary (delay), y = reserved, z = channelMaskBits, w = flags
};

static_assert(sizeof(ForceModelGpuRecord) == 64u, "ForceModelGpuRecord must remain exactly 64 bytes for SSBO alignment.");

} // namespace Frontier::SimulationSpace::ForceFields
