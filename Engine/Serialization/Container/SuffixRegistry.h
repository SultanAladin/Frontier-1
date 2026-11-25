/*============================================================================================================================================
                                                            SUFFIXREGISTRY.H
============================================================================================================================================*/
// 🧩 Authoritative Frontier format suffix registry enforcing the *Space stratification rule and Option 2A case-insensitive resolution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FormatPillarGroup : uint8_t
{
    Configuration = 0u, // [-] - EngineSettings, RuntimeConfiguration, PlatformSettings, ProjectIndex, GraphicsSettings, UserProfile
    World         = 1u, // [-] - WorldSpace, Instance, Component, Terrain
    Visual        = 2u, // [-] - GeometrySpace, SurfaceParameter, Sketch, ConvexHull, PixelSpace, ImageSpace, TextureSpace, VectorSpace, ColourSpace, Pigment, FontSpace
    Optics        = 3u, // [-] - MaterialSpace, ShaderNetwork, ComputeProgram, SpectralCurve, NoiseNetwork, Atmosphere, CameraOptics, PostStack, RenderSequence, IesProfile, Photometry, Irradiance, RadianceClipmap, RayStructure, DistanceField
    Simulation    = 4u, // [-] - PhysicsSpace, FractureNetwork, ShardCluster, ConstraintSet, ImpactProfile, VolumetricSpace, AudioSpace, Soundscape, ParticleSpace, SpawnSource, ForceField, FluidGrid, RibbonTrail, SwarmBehavior
    Logic         = 5u, // [-] - BehaviorSpace, MotionSpace, TriggerZone, EventBus, VariableRegister, TimerProfile, Curve, Timeline
    Interface     = 6u  // [-] - InterfaceSpace, Tooltip, WorkspaceLayout, InputSpace, NetworkSpace
};

enum class FormatSuffixCategory : uint16_t
{
    // --- Tier A: Foundational Top-Level Pillars (*Space ONLY — nothing higher above them) ---
    WorldSpace        = 0u,  // [-] - Top-level world containment, placements & environment links
    GeometrySpace     = 1u,  // [-] - 3D PolygonNetwork, creased normals & ClusterPyramid LODs
    MaterialSpace     = 2u,  // [-] - 10-lobe OpenPBR 1.1 + Glinty surface definitions
    TextureSpace      = 3u,  // [-] - Multi-UDIM channel-packed BC7/ASTC GPU mipchains
    ImageSpace        = 4u,  // [-] - Linear-light HDR/SDR layered canvas & optical sheets
    PixelSpace        = 5u,  // [-] - Packed sprite/icon sheets, virtual page directories & pixel layouts
    VectorSpace       = 6u,  // [-] - SVG/Bezier contours, stencils, variable strokes & vector layers
    ColourSpace       = 7u,  // [-] - CIE XYZ, ACEScg, Rec.2020, Oklab primaries, swatches & ramps
    FontSpace         = 8u,  // [-] - Baked .ttf/.otf character metrics, kerning & MSDF/SDF pages
    ParticleSpace     = 9u,  // [-] - Multi-source particulate simulation (embeds ForceField, ColourSpace, Curve)
    PhysicsSpace      = 10u, // [-] - Rigid dynamics bodies, inertia, vehicle powertrain & world physics
    VolumetricSpace   = 11u, // [-] - PlanetaryClouds, CondensateNoiseVolume & height/local fog
    AudioSpace        = 12u, // [-] - Spatial acoustics, reverb zones, occlusion & PCM/Opus routing
    MotionSpace       = 13u, // [-] - Kinematic rigs, procedural dynamics & motion blending
    BehaviorSpace     = 14u, // [-] - Declarative & compiled behavior logic controlling instances
    InterfaceSpace    = 15u, // [-] - Runtime HUD screens, anchors, non-kinship nesting & z-order
    NetworkSpace      = 16u, // [-] - Session routing, transport failover & differential replication
    InputSpace        = 17u, // [-] - Hardware chord registers, navigation gestures & axis calibration

    // --- Tier B: Specialized Sub-Formats (Never end in *Space) ---
    // Configuration & Layout
    EngineSettings       = 18u, // [-] - Boot tick rate, memory ceilings, Vulkan 1.3 & startup world
    RuntimeConfiguration = 19u, // [-] - Active profile toggles, thread lanes & diagnostic switches
    PlatformSettings     = 20u, // [-] - Windows/Linux/Android/Headless hardware overrides
    ProjectIndex         = 21u, // [-] - Project root directory, startup world link & dependency locks
    GraphicsSettings     = 22u, // [-] - Display resolution, V-Sync, FPS cap & shadow/texture quality
    UserProfile          = 23u, // [-] - Local player preferences, accessibility & playtime (outside Git)
    WorkspaceLayout      = 24u, // [-] - Editor-only dock splits, snap sizes & gizmo scale (excluded from Shipping)

    // World & Embedding
    Instance             = 25u, // [-] - Multi-pillar embedded package (e.g. VehicleDefault.Instance)
    Component            = 26u, // [-] - Reusable typed attachment attached onto an Instance
    Terrain              = 27u, // [-] - Heightfield tiles, biome masks & splat weights

    // Geometry & Visual Sub-Formats
    SurfaceParameter     = 28u, // [-] - UDIM 1001+ & .UVTile parameterization, seams & pins
    Sketch               = 29u, // [-] - Parametric B-Rep / NURBS 2D & 3D CAD drafting
    ConvexHull           = 30u, // [-] - Simplified outer convex polyhedra & origin stamp
    Pigment              = 31u, // [-] - Natural media brush, bristle, dab & stylus stroke presets

    // Optics, Atmosphere & Rendering
    ShaderNetwork        = 32u, // [-] - Authoring network compiled via slangc .slang -> SPIR-V .spv
    ComputeProgram       = 33u, // [-] - Standalone .slang compute program -> SPIR-V .spv
    SpectralCurve        = 34u, // [-] - Wavelength dispersion for thin-film iridescence & prisms
    NoiseNetwork         = 35u, // [-] - Deterministic Simplex/Worley/Perlin/Ridged procedural noise
    Atmosphere           = 36u, // [-] - Sky Rayleigh/Mie/Ozone 3-LUT, solar/lunar/star & aerial fog
    CameraOptics         = 37u, // [-] - Complete lens & sensor: focal length, groups, bokeh, CA, vignette, ISO
    PostStack            = 38u, // [-] - ACES tone curves, 3D grading LUTs, veiling glare & film response
    RenderSequence       = 39u, // [-] - Acyclic render steps, transient VRAM aliasing & async compute sync
    IesProfile           = 40u, // [-] - Measured LM-63 Class C candela distributions + 128x32 LUT
    Photometry           = 41u, // [-] - Solar ephemeris & clipped-horizon rectangular area luminaires
    Irradiance           = 42u, // [-] - Precomputed diffuse bounce textures & directional SH lobes
    RadianceClipmap      = 43u, // [-] - World-space surfel/clipmap GI configuration & checkpoint
    RayStructure         = 44u, // [-] - Portable geometry build specification + device-keyed BVH4/BLAS/TLAS
    DistanceField        = 45u, // [-] - Bounded 3D scalar SDF/UDF grids with sign convention & units

    // Simulation, Destruction & Acoustics Sub-Formats
    FractureNetwork      = 46u, // [-] - Voronoi shatter connectivity network & Gc stress thresholds
    ShardCluster         = 47u, // [-] - Pre-fractured brittle shards, mass, inertia & sleep velocity
    ConstraintSet        = 48u, // [-] - Jolt v5.6.0 joints, ragdoll hinges, pistons & BreakableBonds
    ImpactProfile        = 49u, // [-] - Material-pair collision response (sparks, decals, fracture)
    ForceField           = 50u, // [-] - Physical force/velocity 3D field (embeds optional .VectorField grid)
    FluidGrid            = 51u, // [-] - Eulerian 2D/3D Navier-Stokes velocity, pressure, vorticity & divergence
    SpawnSource          = 52u, // [-] - Particle spawn rates, burst curves & lifetime gradients
    RibbonTrail          = 53u, // [-] - Strip-geometry generators for arcs, contrails & lightning
    SwarmBehavior        = 54u, // [-] - Boids, flocking & GPU-instanced crowd/spark rules
    Soundscape           = 55u, // [-] - Ambient beds, dynamic music stems & spatial bus snapshots

    // Logic, Sequencing & Interface Sub-Formats
    TriggerZone          = 56u, // [-] - Non-blocking sensor bounds firing entry/stay/exit signals
    EventBus             = 57u, // [-] - Named signal schemas & decoupled participant routing
    VariableRegister     = 58u, // [-] - Global switches, counters, multipliers & diagnostic overrides
    TimerProfile         = 59u, // [-] - Reusable countdowns, cooldowns & time-dilation presets
    Curve                = 60u, // [-] - 1D/2D easing & response curves shared across UI/motion/audio
    Timeline             = 61u, // [-] - Multi-track keyframe sequencer (motion, cameras, audio, captions)
    Tooltip              = 62u, // [-] - Hover description layouts, stat comparisons & status indicators

    // Deformable Continuum & Baked Vertex Motion Sub-Formats
    SoftBody             = 63u, // [-] - Unified XPBD + 3D Stable Neo-Hookean FEM deformable preset & rest cage
    VertexAtlas          = 64u  // [-] - Baked 256-column tiled RGBA32F Vertex Motion Atlas (VAT) + axle poses
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FormatSuffixDescriptor
{
    FormatSuffixCategory Category           = FormatSuffixCategory::WorldSpace; // [-]     - Canonical format ordinal
    FormatPillarGroup    PillarGroup        = FormatPillarGroup::World;         // [-]     - Owning serialization pillar group
    std::string_view     CanonicalSuffix    {};                                 // [-]     - Canonical PascalCase disk suffix (Option 2A)
    uint32_t             DefaultSchema      = 1u;                               // [ver]   - Current schema version
    bool                 FoundationalPillar = false;                            // [-]     - True strictly for Tier A *Space pillars
    bool                 EditorOnly         = false;                            // [-]     - True for formats excluded from Shipping
    bool                 SupportsEmbedding  = true;                             // [-]     - Permitted inside .Instance / .WorldSpace containers
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::span<const FormatSuffixDescriptor> EnumerateFormatSuffixDescriptors() noexcept;

[[nodiscard]] const FormatSuffixDescriptor& InspectFormatSuffixDescriptor(FormatSuffixCategory Category) noexcept;

[[nodiscard]] std::string_view QueryCanonicalSuffix(FormatSuffixCategory Category) noexcept;

[[nodiscard]] bool QueryFoundationalPillar(FormatSuffixCategory Category) noexcept;

[[nodiscard]] bool QueryEditorOnlyFormat(FormatSuffixCategory Category) noexcept;

[[nodiscard]] bool ResolveSuffixCaseInsensitive(
    std::string_view      RawSuffix,
    FormatSuffixCategory& OutCategory,
    std::string&          OutCanonicalSuffix,
    std::string&          OutDiagnostic);

[[nodiscard]] bool VerifySpaceStratificationRule(std::string& OutDiagnostic);

} // namespace Frontier::Serialization
