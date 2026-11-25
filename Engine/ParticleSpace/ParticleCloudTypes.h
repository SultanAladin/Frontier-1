/*============================================================================================================================================
                                                          PARTICLECLOUDTYPES.H
============================================================================================================================================*/
// 🧩 Portable 1:1 particle cloud enumerations, parameter layouts, colour presets, and sizing formulas
// Reference: Experimental/FluidEditor/Engine/ParticleCatalog.js L1-L276

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kMaximumReferenceParticles = 16384u;  // [-] - Reference WebGL2 cap (ParticleCatalog.js L17)
inline constexpr uint32_t kMaximumVulkanParticles    = 262144u; // [-] - Frontier Vulkan SSBO ceiling
inline constexpr uint32_t kMaximumColourStops        = 8u;      // [-] - Maximum gradient stops per cloud
inline constexpr uint32_t kMaximumTrailLength        = 96u;     // [-] - Maximum trail history samples
inline constexpr uint32_t kMaximumTrailTexels        = 8192u;   // [-] - Reference GPU trail height limit

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleShapeOrdinal : uint32_t
{
    Point         = 0u,
    Sphere        = 1u,
    Box           = 2u,
    Disc          = 3u,
    Ring          = 4u,
    Line          = 5u,
    Grid          = 6u,
    Torus         = 7u,
    SurfaceShell  = 8u,
    MocapFigure   = 9u,
    Dome          = 10u,
    SolidSurface  = 11u, // TOML/JS token: "ObjectSurface"
    Spiderweb     = 12u,
    ClothGrid     = 13u,
    FloraBed      = 14u,
    FireworkBurst = 15u,
};

enum class ParticleSpawnModeOrdinal : uint32_t
{
    Release = 0u, // TOML/JS token: "Emit"
    Cloner  = 1u,
};

enum class ParticleColourModeOrdinal : uint32_t
{
    Life         = 0u,
    Speed        = 1u,
    Displacement = 2u,
    Height       = 3u,
    Seed         = 4u,
    Swatch       = 5u,
    Cycle        = 6u,
};

enum class ParticleRampSourceOrdinal : uint32_t
{
    SwatchStops    = 0u, // TOML/JS token: "Palette"
    ScientificRamp = 1u, // TOML/JS token: "Ramp"
};

enum class ParticleBlendOrdinal : uint32_t
{
    Alpha    = 0u,
    Additive = 1u,
};

enum class ParticleRenderStyleOrdinal : uint32_t
{
    Point  = 0u,
    Streak = 1u,
};

enum class ParticleSpriteOrdinal : uint32_t
{
    Glow           = 0u,
    Disc           = 1u,
    Bokeh          = 2u,
    Star           = 3u,
    Square         = 4u,
    Cube3D         = 5u,
    Butterfly      = 6u,
    HexTile        = 7u,
    HoloGlyph      = 8u,
    SnowCrystal    = 9u,
    RainDrop       = 10u,
    LeafBlade      = 11u,
    BlossomPetal   = 12u,
    CrystalShard2D = 13u,
    ShockRing      = 14u,
    RadarArc       = 15u,
    AuroraCurtain  = 16u,
    CoinDisc       = 17u,
};

enum class ParticleCarrierOrdinal : uint32_t
{
    Sprite   = 0u,
    Object3D = 1u,
};

enum class ParticleBackingSurfaceOrdinal : uint32_t
{
    Hidden         = 0u,
    ObsidianGlossy = 1u,
    BrushedChrome  = 2u,
    GhostWireframe = 3u,
};

enum class SolidSampleModeOrdinal : uint32_t
{
    EvenSurface   = 0u,
    SurfaceVolume = 1u,
    RidgeAccent   = 2u,
};

enum class ParticleSizeModeOrdinal : uint32_t
{
    World  = 0u,
    Pixels = 1u,
};

enum class ParticleDerezOrdinal : uint32_t
{
    Off            = 0u,
    Derez          = 1u,
    ReverseDerez   = 2u,
    MeltDissolve   = 3u,
    ButterflyMorph = 4u,
    CellGlitch     = 5u,
};

enum class ParticleMocapClipOrdinal : uint32_t
{
    Off      = 0u,
    Sprint   = 1u,
    Dance    = 2u,
    Martial  = 3u,
    Levitate = 4u,
};

enum class ParticleEffectorOrdinal : uint32_t
{
    Off        = 0u,
    Ripple     = 1u,
    Orbiters   = 2u,
    PlaneWave  = 3u,
    NoiseField = 4u,
};

enum class ParticleEffectorAxisOrdinal : uint32_t
{
    Radial        = 0u,
    HeightY       = 1u,
    SurfaceNormal = 2u,
};

enum class ParticleDistortionOrdinal : uint32_t
{
    Off               = 0u,
    GravitationalLens = 1u,
    ShockwaveRipple   = 2u,
    HeatBubble        = 3u,
};

enum class FireworkPatternOrdinal : uint32_t
{
    ClassicBouquet = 0u,
    SkyText        = 1u,
    Heart          = 2u,
    Star           = 3u,
    Smiley         = 4u,
    Saturn         = 5u,
    Crown          = 6u,
    Butterfly      = 7u,
    Spiral         = 8u,
    GrandFinale    = 9u,
};

enum class ParticleStrandOrdinal : uint32_t
{
    Off          = 0u,
    OrbWeaveWeb  = 1u,
    MessyCobweb  = 2u,
    CablesChains = 3u,
    HangingVines = 4u,
    TallReeds    = 5u,
    WoodlandFern = 6u,
    GiantKelp    = 7u,
    SeaAnemone   = 8u,
    TeslaArcs    = 9u,
    PlasmaCage   = 10u,
    StormWeb     = 11u,
};

enum class ParticleStrandVariantOrdinal : uint32_t
{
    Primary   = 0u,
    Secondary = 1u,
};

enum class ParticleTrailModeOrdinal : uint32_t
{
    Off    = 0u,
    Ribbon = 1u,
    Only   = 2u,
};

enum class ParticleTrailColourOrdinal : uint32_t
{
    Particle = 0u,
    Along    = 1u,
};

enum class ParticleXpbdTopologyOrdinal : uint8_t
{
    Off          = 0u,
    ClothGrid    = 1u,
    Spiderweb    = 2u,
    CablesChains = 3u,
    FloraBed     = 4u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SIZING HELPERS
//------------------------------------------------------------------------------------------------------------------------

/// Square texture side that holds `count` particles (ParticleCatalog.js L271-L275).
[[nodiscard]] inline uint32_t ComputeParticleTextureSide(const uint32_t count) noexcept
{
    const uint32_t clamped = std::clamp(count, 16u, kMaximumReferenceParticles);
    const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(clamped))));
    const uint32_t rounded = ((side + 3u) / 4u) * 4u;
    return std::clamp(rounded, 4u, 128u);
}

/// History samples actually allocated within GPU texture height limits (ParticleCatalog.js L64-L67).
[[nodiscard]] inline uint32_t ComputeEffectiveTrailLength(const uint32_t count, const uint32_t requested) noexcept
{
    const uint32_t side = ComputeParticleTextureSide(count);
    const uint32_t cap  = kMaximumTrailTexels / std::max(1u, side);
    return std::max(2u, std::min(requested, cap));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Evaluated flat GPU parameters of a particle cloud (1:1 with ParticleCatalog.js L125-L265).
struct ParticleCloudParameters
{
    // Spawn & shape
    ParticleSpawnModeOrdinal      Mode              = ParticleSpawnModeOrdinal::Release;
    ParticleShapeOrdinal          Shape             = ParticleShapeOrdinal::Point;
    float                         ShapeDetail       = 8.0f;
    std::string                   SpawnSolid        = "UtahTeapot";
    SolidSampleModeOrdinal        SpawnSampleMode   = SolidSampleModeOrdinal::EvenSurface;
    float                         SolidYawDegrees   = 0.0f;
    ParticleBackingSurfaceOrdinal BackingSurface    = ParticleBackingSurfaceOrdinal::Hidden;
    std::string                   CustomSolidId     {};
    FireworkPatternOrdinal        FireworkPattern   = FireworkPatternOrdinal::ClassicBouquet;
    std::string                   FireworkText      = "2026";
    std::array<float, 3>          Position          = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>          Direction         = { 0.0f, 1.0f, 0.0f };
    float                         Radius            = 0.5f;
    uint32_t                      Count             = 2048u;
    float                         Rate              = 8.0f;
    float                         Lifetime          = 3.0f;
    float                         Fill              = 0.0f;
    float                         Linger            = 0.6f;

    // Motion & forces
    float                         Dissolve          = 0.0f;
    float                         NoiseScale        = 1.3f;
    float                         NoiseSpeed        = 0.5f;
    float                         Speed             = 2.0f;
    float                         Spread            = 0.5f;
    float                         Radial            = 0.0f;
    float                         Gravity           = 0.0f;
    float                         Drag              = 0.5f;
    float                         Noise             = 0.0f;
    float                         FieldInfluence    = 1.0f;
    float                         GasInfluence      = 0.0f;
    float                         Spring            = 6.0f;
    float                         Damping           = 3.0f;
    float                         Bounce            = 0.2f;

    // Sizing, sprites & 3D carriers
    float                         SizeStart         = 3.0f;
    float                         SizeEnd           = 1.0f;
    float                         SizeRandom        = 0.3f;
    ParticleSizeModeOrdinal       SizeMode          = ParticleSizeModeOrdinal::Pixels;
    ParticleSpriteOrdinal         Sprite            = ParticleSpriteOrdinal::Disc;
    ParticleCarrierOrdinal        CarrierMode       = ParticleCarrierOrdinal::Sprite;
    std::string                   CarrierSolid      = "Cube";
    std::string                   CarrierCustomId   {};
    float                         SolidSpin         = 1.2f;
    float                         SolidTilt         = 0.15f;
    float                         SolidRoughness    = 0.35f;
    float                         SolidMetallic     = 0.0f;
    float                         SolidGlow         = 0.0f;
    float                         Glow              = 0.0f;
    float                         FocusDistance     = 0.0f;
    float                         Aperture          = 0.5f;

    // Colour & shading
    ParticleColourModeOrdinal     ColourMode        = ParticleColourModeOrdinal::Life;
    std::vector<std::string>      ColourStops       = { "#ffffff", "#808080" };
    ParticleRampSourceOrdinal     RampSource        = ParticleRampSourceOrdinal::SwatchStops;
    float                         HueJitter         = 0.0f;
    float                         BrightnessJitter  = 0.0f;
    float                         CycleSpeed        = 0.5f;
    float                         Brightness        = 1.0f;
    std::string                   RampName          = "Turbo";
    float                         DisplacementRange = 1.0f;
    float                         MaxSpeed          = 6.0f;
    float                         Flicker           = 0.0f;
    ParticleBlendOrdinal          Blend             = ParticleBlendOrdinal::Alpha;
    ParticleRenderStyleOrdinal    Style             = ParticleRenderStyleOrdinal::Point;

    // Trails
    ParticleTrailModeOrdinal      Trail             = ParticleTrailModeOrdinal::Off;
    uint32_t                      TrailLength       = 24u;
    float                         TrailInterval     = 0.08f;
    float                         TrailWidth        = 0.01f;
    float                         TrailTaper        = 0.6f;
    float                         TrailFade         = 0.7f;
    uint32_t                      TrailSubdivisions = 2u;
    ParticleTrailColourOrdinal    TrailColour       = ParticleTrailColourOrdinal::Particle;
    float                         TrailRatio        = 1.0f;

    // Derez & Dissolve
    ParticleDerezOrdinal          DerezMode         = ParticleDerezOrdinal::Off;
    float                         DerezMix          = 0.0f;
    float                         DerezProgress     = 0.0f;
    float                         DerezAutoSpeed    = 0.0f;
    float                         DerezEdgeWidth    = 0.14f;
    float                         DerezEdgeGlow     = 0.0f;
    std::string                   DerezEdgeColour   = "#00f0ff";
    float                         DerezShatter      = 0.0f;

    // Mocap performer
    ParticleMocapClipOrdinal      MocapClip         = ParticleMocapClipOrdinal::Off;
    float                         MocapAmplitude    = 0.0f;
    float                         MocapSpeed        = 1.0f;
    float                         MocapScale        = 1.0f;
    float                         MocapTrailKick    = 0.0f;

    // MoGraph effector
    ParticleEffectorOrdinal       EffectorMode        = ParticleEffectorOrdinal::Off;
    float                         EffectorStrength    = 0.0f;
    float                         EffectorFrequency   = 1.8f;
    float                         EffectorSpeed       = 1.6f;
    float                         EffectorPush        = 0.0f;
    float                         EffectorScaleBoost  = 0.0f;
    float                         EffectorColourShift = 0.0f;
    ParticleEffectorAxisOrdinal   EffectorAxis        = ParticleEffectorAxisOrdinal::Radial;

    // Flock & Swarm
    float                         SwarmStrength     = 0.0f;
    float                         SwarmCohesion     = 0.0f;
    float                         SwarmOrbit        = 0.0f;
    float                         SwarmWingRate     = 10.0f;

    // Glitch overlay
    float                         GlitchAmount      = 0.0f;
    float                         GlitchRate        = 14.0f;
    float                         GlitchStepJitter  = 0.0f;
    float                         GlitchScanlines   = 0.0f;
    float                         GlitchChromatic   = 0.0f;

    // Distortion & lensing overlay
    ParticleDistortionOrdinal     DistortionMode      = ParticleDistortionOrdinal::Off;
    float                         DistortionStrength  = 0.0f;
    float                         DistortionRadius    = 0.28f;
    float                         DistortionHorizon   = 0.0f;
    float                         DistortionChromatic = 0.0f;

    // Strands, webs & arcs
    ParticleStrandOrdinal         StrandMode        = ParticleStrandOrdinal::Off;
    ParticleStrandVariantOrdinal  StrandVariant     = ParticleStrandVariantOrdinal::Primary;
    uint32_t                      StrandCount       = 36u;
    uint32_t                      StrandDetail      = 14u;
    float                         StrandSag         = 0.0f;
    float                         StrandThickness   = 0.032f;
    float                         StrandSway        = 0.0f;
    float                         StrandSpan        = 1.8f;
    float                         StrandGlow        = 0.0f;

    // Timing envelope
    float                         StartTime         = 0.0f;
    float                         Duration          = 0.0f;
    float                         Fade              = 0.15f;
    float                         Repeat            = 0.0f;

    // Additive Layer 220: Frontier GPU Orthotropic XPBD (all neutral / off by default)
    ParticleXpbdTopologyOrdinal   XpbdTopology      = ParticleXpbdTopologyOrdinal::Off;
    uint32_t                      XpbdSubsteps      = 1u;
    uint32_t                      XpbdIterations    = 1u;
    float                         XpbdWarpModulus   = 0.0f;
    float                         XpbdWeftModulus   = 0.0f;
    float                         XpbdShearModulus  = 0.0f;
    float                         XpbdBendStiffness = 0.0f;
    float                         XpbdFriction      = 0.0f;
    float                         XpbdBreakRatio    = 0.0f;
    float                         XpbdAeroDrag      = 0.0f;

    // Additive Layer 221: Frontier GPU Collisions (all neutral / off by default)
    bool                          CollisionRigidCoupling  = false;
    bool                          CollisionSelfHash       = false;
    bool                          CollisionTwoWayImpulse  = false;
    float                         CollisionParticleRadius = 0.0f;
    float                         CollisionRestitution    = 0.0f;
    float                         CollisionFriction       = 0.0f;
    float                         CollisionCohesion       = 0.0f;
    float                         CollisionPositionRelax  = 0.0f;
    uint32_t                      CollisionSubsteps       = 1u;
    uint32_t                      CollisionIterations     = 1u;
};

struct ParticleCloudNode
{
    std::string                                        NodeId     {};
    std::string                                        TemplateId {};
    std::string                                        Name       {};
    bool                                               Enabled    = true;
    bool                                               Visible    = true;
    bool                                               Halted     = false;
    SimulationSpace::EffectLayers::EffectLayerStack    Stack      {};
    ParticleCloudParameters                            Evaluated  {};
    std::vector<SimulationSpace::ForceFields::KeyTrack> Tracks    {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           TOKEN PARSING & QUERY TABLES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::optional<ParticleShapeOrdinal>          ParseParticleShapeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleSpawnModeOrdinal>      ParseParticleSpawnModeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleColourModeOrdinal>     ParseParticleColourModeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleRampSourceOrdinal>     ParseParticleRampSourceToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleBlendOrdinal>          ParseParticleBlendToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleRenderStyleOrdinal>    ParseParticleRenderStyleToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleSpriteOrdinal>         ParseParticleSpriteToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleCarrierOrdinal>        ParseParticleCarrierToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleBackingSurfaceOrdinal> ParseParticleBackingSurfaceToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<SolidSampleModeOrdinal>        ParseSolidSampleModeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleSizeModeOrdinal>       ParseParticleSizeModeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleDerezOrdinal>          ParseParticleDerezToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleMocapClipOrdinal>      ParseParticleMocapClipToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleEffectorOrdinal>       ParseParticleEffectorToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleEffectorAxisOrdinal>   ParseParticleEffectorAxisToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleDistortionOrdinal>     ParseParticleDistortionToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<FireworkPatternOrdinal>        ParseFireworkPatternToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleStrandOrdinal>         ParseParticleStrandToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleStrandVariantOrdinal>  ParseParticleStrandVariantToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleTrailModeOrdinal>      ParseParticleTrailModeToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleTrailColourOrdinal>    ParseParticleTrailColourToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<ParticleXpbdTopologyOrdinal>   ParseParticleXpbdTopologyToken(std::string_view token) noexcept;

[[nodiscard]] std::string_view QueryFireworkPatternToken(FireworkPatternOrdinal pattern) noexcept;
[[nodiscard]] std::span<const std::string_view> QueryParticleCategoryNames() noexcept;

} // namespace Frontier::ParticleSpace
